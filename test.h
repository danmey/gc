#ifndef TEST_H
#define TEST_H

#define BEGIN_TEST(id)	\
    void test_##id() \
    { \
    char* curr_test = #id; \
    printf("Running test "#id"\n");


#define END_TEST() \
    printf("Test %s completed\n", curr_test);		\
    }

#endif

