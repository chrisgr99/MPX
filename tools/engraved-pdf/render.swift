import Foundation
import CoreGraphics
import ImageIO
import UniformTypeIdentifiers

let args = CommandLine.arguments
let url = URL(fileURLWithPath: args[1])
let scale: CGFloat = CGFloat(Double(args[2]) ?? 4.0)
guard let doc = CGPDFDocument(url as CFURL) else { exit(1) }
for i in 1...doc.numberOfPages {
    guard let page = doc.page(at: i) else { continue }
    let r = page.getBoxRect(.mediaBox)
    let w = Int(r.width*scale), h = Int(r.height*scale)
    let cs = CGColorSpaceCreateDeviceRGB()
    guard let ctx = CGContext(data: nil, width: w, height: h, bitsPerComponent: 8,
        bytesPerRow: 0, space: cs, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { continue }
    ctx.setFillColor(CGColor(red:1,green:1,blue:1,alpha:1))
    ctx.fill(CGRect(x:0,y:0,width:w,height:h))
    ctx.scaleBy(x: scale, y: scale)
    ctx.translateBy(x: -r.origin.x, y: -r.origin.y)
    ctx.drawPDFPage(page)
    guard let img = ctx.makeImage() else { continue }
    let prefix = args.count > 3 ? args[3] : "pages/render"
    let out = URL(fileURLWithPath: "\(prefix)\(i).png")
    guard let dest = CGImageDestinationCreateWithURL(out as CFURL, UTType.png.identifier as CFString, 1, nil) else { continue }
    CGImageDestinationAddImage(dest, img, nil)
    CGImageDestinationFinalize(dest)
    print("page \(i) \(w)x\(h)")
}
