#define WL 16
#define IWL 8

using namespace sc_dt;
using namespace std;

typedef sc_fixed<WL, IWL> fixed_t;

fixed_t fixed_heron_sqrt(fixed_t s);