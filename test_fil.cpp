/**
 * @file test_fil.cpp
 * @brief GTest unit tests for FIL module
 *
 * Build:
 *   g++ -o test_fil test_fil.cpp fil.c \
 *       -lgtest -lgtest_main -lpthread \
 *       --coverage -fprofile-arcs -ftest-coverage \
 *       -g -O0 -I.
 *
 * Coverage:
 *   gcov fil.c
 *   lcov --capture --directory . --output-file coverage.info
 *   genhtml coverage.info --output-directory coverage_report
 */

#include <gtest/gtest.h>
#include <cstring>
#include <cstdlib>

extern "C" {
#include "fil.h"
#include "types.h"
}

/* =========================================================
 * Test Fixture
 * ========================================================= */
class FILTest : public ::testing::Test {
protected:
    MCA_t  mca;
    char   buffer[4096];

    void SetUp() override {
        memset(CMDQHead, 0, sizeof(CMDQHead));
        memset(CMDQTail, 0, sizeof(CMDQTail));
        FC_RESP_READY = FALSE;
        RP = NULL;
        FIL_Init();

        mca.ch       = 0;
        mca.bnk      = 0;
        mca.priority = 0;
        memset(buffer, 0xAB, sizeof(buffer));
    }

    void TearDown() override {
        if (RP) { free(RP); RP = NULL; }
    }

    /* Helper — inject a mock completion so the while loop exits */
    void MockCompletion(uint8_t result = 0) {
        if (RP == NULL)
            RP = (FCReq_t *)calloc(1, sizeof(FCReq_t));
        RP->DW00.result = result;
        FC_RESP_READY   = TRUE;
    }
};

/* =========================================================
 * FIL_Init
 * ========================================================= */
TEST_F(FILTest, Init_SetsAllQueuePointersToZero) {
    FIL_Init();
    for (int ch = 0; ch < MAX_CHANNEL; ch++)
        for (int bk = 0; bk < MAX_BANK; bk++) {
            EXPECT_EQ(CMDQHead[ch][bk], 0);
            EXPECT_EQ(CMDQTail[ch][bk], 0);
        }
}

/* =========================================================
 * FIL_RP_Create
 * ========================================================= */
TEST_F(FILTest, RPCreate_TaskID) {
    FIL_RP_Create(OPC_WRITE, &mca, buffer);
    EXPECT_EQ(RP->DW00.taskID, OPC_WRITE);
}

TEST_F(FILTest, RPCreate_RPID_FromTail) {
    CMDQTail[0][0] = 2;
    FIL_RP_Create(OPC_WRITE, &mca, buffer);
    EXPECT_EQ(RP->DW00.rpID, 2);
}

TEST_F(FILTest, RPCreate_BankAndChannel) {
    mca.ch = 1; mca.bnk = 2;
    FIL_RP_Create(OPC_READ, &mca, buffer);
    EXPECT_EQ(RP->DW00.channel, 1);
    EXPECT_EQ(RP->DW00.bank,    2);
}

TEST_F(FILTest, RPCreate_ResultInitZero) {
    FIL_RP_Create(OPC_WRITE, &mca, buffer);
    EXPECT_EQ(RP->DW00.result, 0x0);
}

TEST_F(FILTest, RPCreate_DW02_IsBufferAddress) {
    FIL_RP_Create(OPC_WRITE, &mca, buffer);
    EXPECT_EQ(RP->DW02, (uint32_t)(uintptr_t)buffer);
}

TEST_F(FILTest, RPCreate_ByteCount_Erase) {
    FIL_RP_Create(OPC_ERASE, &mca, buffer);
    EXPECT_EQ(RP->DW03.bytes, N_BYTES_ERASE);
}

TEST_F(FILTest, RPCreate_ByteCount_Write) {
    FIL_RP_Create(OPC_WRITE, &mca, buffer);
    EXPECT_EQ(RP->DW03.bytes, N_BYTES_WRITE);
}

TEST_F(FILTest, RPCreate_ByteCount_Read) {
    FIL_RP_Create(OPC_READ, &mca, buffer);
    EXPECT_EQ(RP->DW03.bytes, N_BYTES_READ);
}

TEST_F(FILTest, RPCreate_ByteCount_UnknownOpcodeZero) {
    FIL_RP_Create(0xFF, &mca, buffer);
    EXPECT_EQ(RP->DW03.bytes, 0x0000);
}

TEST_F(FILTest, RPCreate_DW13_MsgFFFF) {
    FIL_RP_Create(OPC_WRITE, &mca, buffer);
    EXPECT_EQ(RP->DW13.msg, 0xFFFF);
}

TEST_F(FILTest, RPCreate_Planes_MaxMinusOne) {
    FIL_RP_Create(OPC_WRITE, &mca, buffer);
    EXPECT_EQ(RP->DW00.planes, MAX_PLANE - 1);
}

/* =========================================================
 * FIL_RequestHandler — queue full
 * ========================================================= */
TEST_F(FILTest, RequestHandler_ReturnsFalse_QueueFull) {
    CMDQTail[0][0] = 3;
    CMDQHead[0][0] = 0;   /* (3+1)%4 == 0 == Head → full */
    EXPECT_EQ(FIL_RequestHandler(OPC_WRITE, &mca, buffer), FALSE);
}

TEST_F(FILTest, RequestHandler_TailUnchanged_WhenFull) {
    CMDQTail[0][0] = 3;
    CMDQHead[0][0] = 0;
    FIL_RequestHandler(OPC_WRITE, &mca, buffer);
    EXPECT_EQ(CMDQTail[0][0], 3);
}

/* =========================================================
 * FIL_RequestHandler — normal enqueue
 * ========================================================= */
TEST_F(FILTest, RequestHandler_AdvancesTail_OnSuccess) {
    MockCompletion(0);
    FIL_RequestHandler(OPC_WRITE, &mca, buffer);
    EXPECT_EQ(CMDQTail[0][0], 1);
}

TEST_F(FILTest, RequestHandler_WritesCMDQEntry_AtTailSlot) {
    CMDQTail[0][0] = 1;
    CMDQHead[0][0] = 0;
    MockCompletion(0);
    FIL_RequestHandler(OPC_READ, &mca, buffer);

    uint8_t *cmdq = (uint8_t *)(uintptr_t)FCSim_GetCMDQBase(0, 0);
    EXPECT_EQ(cmdq[1], 1);   /* rpid == slot == 1 */
}

TEST_F(FILTest, RequestHandler_WritesRP_IntoFRMSlot) {
    MockCompletion(0);
    FIL_RequestHandler(OPC_WRITE, &mca, buffer);

    FCReq_t *frm = (FCReq_t *)(uintptr_t)FCSim_GetFRMBase(0);
    EXPECT_EQ(frm->DW00.taskID, OPC_WRITE);
}

TEST_F(FILTest, RequestHandler_ReturnsTrue_ResultZero) {
    MockCompletion(0);
    EXPECT_EQ(FIL_RequestHandler(OPC_WRITE, &mca, buffer), TRUE);
}

TEST_F(FILTest, RequestHandler_ReturnsFalse_ResultNonZero) {
    MockCompletion(1);   /* fc sets error bit */
    EXPECT_EQ(FIL_RequestHandler(OPC_WRITE, &mca, buffer), FALSE);
}

/* =========================================================
 * FIL_RequestHandler — multi channel/bank
 * ========================================================= */
TEST_F(FILTest, RequestHandler_AdvancesCorrectChannel) {
    mca.ch = 1; mca.bnk = 2;
    MockCompletion(0);
    FIL_RequestHandler(OPC_READ, &mca, buffer);

    EXPECT_EQ(CMDQTail[1][2], 1);
    EXPECT_EQ(CMDQTail[0][0], 0);   /* other ch/bk untouched */
}

/* =========================================================
 * FIL_RequestHandler — tail wraparound
 * ========================================================= */
TEST_F(FILTest, RequestHandler_TailWrapsAt4) {
    CMDQTail[0][0] = 3;
    CMDQHead[0][0] = 1;   /* not full: (3+1)%4=0 != 1 */
    MockCompletion(0);
    FIL_RequestHandler(OPC_WRITE, &mca, buffer);
    EXPECT_EQ(CMDQTail[0][0], 0);   /* (3+1)%4 = 0 */
}

TEST_F(FILTest, RequestHandler_ThreeConsecutiveEnqueues) {
    CMDQHead[0][0] = 0;
    for (uint8_t i = 0; i < 3; i++) {
        MockCompletion(0);
        EXPECT_EQ(FIL_RequestHandler(OPC_READ, &mca, buffer), TRUE)
            << "Failed at enqueue " << (int)i;
    }
    EXPECT_EQ(CMDQTail[0][0], 3);
}

/* =========================================================
 * FIL_CompletionResponse
 * ========================================================= */
TEST_F(FILTest, CompletionResponse_SetsRespReady) {
    RP = (FCReq_t *)calloc(1, sizeof(FCReq_t));
    RPCompletionFormat_t resp = {};
    resp.RP_ID = 0; resp.CHANNEL = 0; resp.BANK = 0;

    FC_RESP_READY = FALSE;
    FIL_CompletionResponse(resp);
    EXPECT_EQ(FC_RESP_READY, TRUE);
}

TEST_F(FILTest, CompletionResponse_CopiesResultBit) {
    /* Plant result=1 in FRM slot 0 */
    FCReq_t *frm = (FCReq_t *)(uintptr_t)FCSim_GetFRMBase(0);
    frm->DW00.result = 1;

    RP = (FCReq_t *)calloc(1, sizeof(FCReq_t));
    RPCompletionFormat_t resp = {};
    resp.RP_ID = 0; resp.CHANNEL = 0; resp.BANK = 0;

    FIL_CompletionResponse(resp);
    EXPECT_EQ(RP->DW00.result, 1);
}

TEST_F(FILTest, CompletionResponse_ReadsCorrectSlot_NonZeroRPID) {
    /* Plant known taskID in slot 2 of bank 0 */
    uint8_t spb = (4096 / MAX_BANK) / sizeof(FCReq_t);
    FCReq_t *frm_slot = (FCReq_t *)((uint8_t *)(uintptr_t)FCSim_GetFRMBase(0)
                        + (0 * spb + 2) * sizeof(FCReq_t));
    frm_slot->DW00.taskID = OPC_READ;

    RP = (FCReq_t *)calloc(1, sizeof(FCReq_t));
    RPCompletionFormat_t resp = {};
    resp.RP_ID = 2; resp.CHANNEL = 0; resp.BANK = 0;

    FIL_CompletionResponse(resp);
    EXPECT_EQ(RP->DW00.taskID, OPC_READ);
}