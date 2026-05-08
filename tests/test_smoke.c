#include "minunit.h"

MU_TEST(test_truth) {
    mu_check(1 == 1);
}

MU_TEST_SUITE(suite) {
    MU_RUN_TEST(test_truth);
}

int main(void) {
    MU_RUN_SUITE(suite);
    MU_REPORT();
    return MU_EXIT_CODE;
}
