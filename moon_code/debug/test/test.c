#include"moon_debug.h"

#ifdef MODENAME
#undef MODENAME
#endif
#define MODENAME "test"
void main(){
	MOON_WARNNING(
MOON_PRINT( ERROR, "", "");
MOON_PRINT( WARNNING, "", "", "asd" );
);
MOON_TEST(
MOON_PRINT( DEBUG, "", "", "asd", "wer");
MOON_PRINT( TEST, "", "");
);

MOON_PRINT_MAN( ERROR, "this is %s:%d", "new", 12 );
}
