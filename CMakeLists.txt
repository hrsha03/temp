/**
 * @file test_<module>.cpp
 *
 * Build:
 *   g++ -o test_<module> test_<module>.cpp <module>.c [deps.c] \
 *       -lgtest -lgtest_main -lpthread \
 *       --coverage -fprofile-arcs -ftest-coverage \
 *       -g -O0 -I.
 */

#include <gtest/gtest.h>
#include <cstring>
#include <cstdlib>

extern "C" {
#include "<module>.h"
#include "types.h"
}

/* =========================================================
 * Mocks for external module dependencies
 * Only mock what <module> calls — don't mock the module under test
 * ========================================================= */

/* Example: if <module> calls L2P_Read_NBA(), provide a stub */
Block_t mock_block = {};
Block_t *L2P_Read_NBA(MCA_t *mca) {
    return &mock_block;
}

/* Example: if <module> calls LBM_UpdateBlockStatus() */
void LBM_UpdateBlockStatus(MCA_t *mca) {
    /* no-op stub — just absorb the call */
}

/* Add one stub per external function <module>.c calls
 * Rule: if it's in another .c file and not under test, stub it */




/* =========================================================
 * Test Fixture
 *
 * What goes in the class:
 *   - Input objects the module functions take as parameters
 *   - Shared global/extern state the module reads or writes
 *   - Stubs/flags to control mock behaviour per test
 *
 * What does NOT go in the class:
 *   - Output validation logic (that goes in each TEST_F)
 *   - Unrelated module state
 * ========================================================= */
class MODULETest : public ::testing::Test {
protected:

    /* --- Inputs --- */
    MCA_t    mca;          /* or whatever the primary input struct is */
    char     buffer[4096];

    /* --- Mock control flags --- */
    /* These let individual tests change stub behaviour */
    bool     mock_dep_should_fail = false;
    uint8_t  mock_dep_return_val  = 0;

    /* --- SetUp: runs before EVERY test --- */
    void SetUp() override {
        /* 1. Reset all extern/global state the module touches */
        memset(CMDQHead, 0, sizeof(CMDQHead));
        memset(CMDQTail, 0, sizeof(CMDQTail));

        /* 2. Reset mock control flags to safe defaults */
        mock_dep_should_fail = false;
        mock_dep_return_val  = 0;
        memset(&mock_block,  0, sizeof(mock_block));

        /* 3. Call the module's own Init — always, every test */
        MODULE_Init();

        /* 4. Set up a default valid input object */
        mca.ch  = 0;
        mca.bnk = 0;
        memset(buffer, 0xAB, sizeof(buffer));
    }

    /* --- TearDown: runs after EVERY test --- */
    void TearDown() override {
        /* Free anything the module allocates internally
         * that you have a handle to */
    }

    /* --- Helpers --- shared logic used across multiple tests --- */

    /* Drive the module into a known "ready" state */
    void DriveToReadyState() {
        MODULE_Init();
        /* any extra setup needed before operation functions work */
    }

    /* Drive the module into a full/error state */
    void FillQueue(uint8_t ch, uint8_t bk) {
        CMDQTail[ch][bk] = 3;
        CMDQHead[ch][bk] = 0;
    }
};

/* =========================================================
 * Group 1 — Init tests
 * Rule: test Init in isolation, no operation functions here
 * ========================================================= */
TEST_F(MODULETest, Init_ResetsStateToKnownGood) {
    /* corrupt state first, then re-init, then verify */
    CMDQHead[0][0] = 0xFF;
    MODULE_Init();
    EXPECT_EQ(CMDQHead[0][0], 0);
}

TEST_F(MODULETest, Init_IdempotentOnMultipleCalls) {
    MODULE_Init();
    MODULE_Init();
    EXPECT_EQ(CMDQHead[0][0], 0);   /* should be safe to call twice */
}

/* =========================================================
 * Group 2 — Operation function: happy path
 * Rule: one behaviour per test, use the most minimal setup
 * ========================================================= */
TEST_F(MODULETest, OperationFn_ReturnsTrue_OnSuccess) {
    bool_t result = MODULE_OperationFn(&mca, buffer);
    EXPECT_EQ(result, TRUE);
}

TEST_F(MODULETest, OperationFn_UpdatesStateCorrectly) {
    MODULE_OperationFn(&mca, buffer);
    EXPECT_EQ(some_global_state, expected_value);
}

/* =========================================================
 * Group 3 — Operation function: error/boundary paths
 * These are the paths gcov will flag as uncovered if missed
 * ========================================================= */
TEST_F(MODULETest, OperationFn_ReturnsFalse_WhenQueueFull) {
    FillQueue(0, 0);
    EXPECT_EQ(MODULE_OperationFn(&mca, buffer), FALSE);
}

TEST_F(MODULETest, OperationFn_HandlesNullInput_Gracefully) {
    EXPECT_EQ(MODULE_OperationFn(NULL, buffer), FALSE);
}

TEST_F(MODULETest, OperationFn_BoundaryValue_MaxChannel) {
    mca.ch = MAX_CHANNEL - 1;
    EXPECT_EQ(MODULE_OperationFn(&mca, buffer), TRUE);
}

/* =========================================================
 * Group 4 — Multi-instance / isolation
 * Verify one channel/bank doesn't bleed into another
 * ========================================================= */
TEST_F(MODULETest, OperationFn_DoesNotAffectOtherChannels) {
    mca.ch = 1; mca.bnk = 2;
    MODULE_OperationFn(&mca, buffer);

    EXPECT_EQ(CMDQTail[0][0], 0);   /* ch0/bk0 must be untouched */
    EXPECT_EQ(CMDQTail[1][2], 1);   /* only ch1/bk2 changed */
}

/* =========================================================
 * Group 5 — Dependency interaction
 * Verify the module reacts correctly to what mocks return
 * ========================================================= */
TEST_F(MODULETest, OperationFn_HandlesDepFailure) {
    mock_dep_should_fail = true;
    EXPECT_EQ(MODULE_OperationFn(&mca, buffer), FALSE);
}