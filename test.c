#include "compile.h"

#define TEST_FILE "/Users/yusuf/make/vm/test.kg"

#ifdef TEST

int main (int argc, char** argv)
{
	interpret_file(TEST_FILE, 0);
}

#endif // TEST