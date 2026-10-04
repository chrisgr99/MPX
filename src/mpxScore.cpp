/** mpxScore, the six-part Score. Everything is in GuitarChart.hpp; see there.

MUSIC WITH NO FINGERBOARD: a fugue, a string quartet, anything written for instruments that are
not fretted. The same machinery the guitar chart uses — it reads MusicXML as well as Guitar Pro,
and both end up as the same song — with the guitar's assumptions out of the way. A note with no
string is one the renderer gives a voice of its own, which is what the bus and the performer have
always said about music of this kind.

ONE PART IS ONE ROW IS ONE CABLE. A quartet is four rows into four modules. A keyboard fugue is
one row: one part, two staves, three or four voices down one cable, overlapping as a fugue must.

SIX ROWS, which covers a quartet, a trio, a wind quintet and a keyboard part with room to spare.
Whether a larger one is wanted beside it is a question to answer by looking at this one. */
#define CHART_ROWS 6
#define CHART_AUDIO 1
#define CHART_NS score6
#define CHART_SLUG "mpxScore"
#define CHART_TITLE "mpxScore"
#define CHART_MODEL modelMpxScore
#include "GuitarChart.hpp"
