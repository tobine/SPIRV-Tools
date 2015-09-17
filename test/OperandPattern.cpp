// Copyright (c) 2015 The Khronos Group Inc.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and/or associated documentation files (the
// "Materials"), to deal in the Materials without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Materials, and to
// permit persons to whom the Materials are furnished to do so, subject to
// the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Materials.
//
// MODIFICATIONS TO THIS FILE MAY MEAN IT NO LONGER ACCURATELY REFLECTS
// KHRONOS STANDARDS. THE UNMODIFIED, NORMATIVE VERSIONS OF KHRONOS
// SPECIFICATIONS AND HEADER INFORMATION ARE LOCATED AT
//    https://www.khronos.org/registry/
//
// THE MATERIALS ARE PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
// CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// MATERIALS OR THE USE OR OTHER DEALINGS IN THE MATERIALS.

#include "UnitSPIRV.h"

#include "gmock/gmock.h"
#include "../source/operand.h"

using ::testing::Eq;

namespace {

TEST(OperandPattern, InitiallyEmpty) {
  spv_operand_pattern_t empty;
  EXPECT_THAT(empty, Eq(spv_operand_pattern_t{}));
  EXPECT_EQ(0u, empty.size());
  EXPECT_TRUE(empty.empty());
}

TEST(OperandPattern, PushFrontsAreOnTheLeft) {
  spv_operand_pattern_t pattern;

  pattern.push_front(SPV_OPERAND_TYPE_ID);
  EXPECT_THAT(pattern, Eq(spv_operand_pattern_t{SPV_OPERAND_TYPE_ID}));
  EXPECT_EQ(1u, pattern.size());
  EXPECT_TRUE(!pattern.empty());
  EXPECT_EQ(SPV_OPERAND_TYPE_ID, pattern.front());

  pattern.push_front(SPV_OPERAND_TYPE_NONE);
  EXPECT_THAT(pattern, Eq(spv_operand_pattern_t{SPV_OPERAND_TYPE_NONE,
                                                SPV_OPERAND_TYPE_ID}));
  EXPECT_EQ(2u, pattern.size());
  EXPECT_TRUE(!pattern.empty());
  EXPECT_EQ(SPV_OPERAND_TYPE_NONE, pattern.front());
}

TEST(OperandPattern, PopFrontsAreOnTheLeft) {
  spv_operand_pattern_t pattern{SPV_OPERAND_TYPE_LITERAL_NUMBER,
                                SPV_OPERAND_TYPE_ID};

  pattern.pop_front();
  EXPECT_THAT(pattern, Eq(spv_operand_pattern_t{SPV_OPERAND_TYPE_ID}));

  pattern.pop_front();
  EXPECT_THAT(pattern, Eq(spv_operand_pattern_t{}));
}

// A test case for typed mask expansion
struct MaskExpansionCase {
  const spv_operand_type_t type;
  const uint32_t mask;
  const spv_operand_pattern_t initial;
  const spv_operand_pattern_t expected;
};

using MaskExpansionTest = ::testing::TestWithParam<MaskExpansionCase>;

TEST_P(MaskExpansionTest, Sample) {
  spv_operand_table operandTable = nullptr;
  ASSERT_EQ(SPV_SUCCESS, spvOperandTableGet(&operandTable));

  spv_operand_pattern_t pattern(GetParam().initial);
  spvPrependOperandTypesForMask(operandTable, GetParam().type, GetParam().mask, &pattern);
  EXPECT_THAT(pattern, Eq(GetParam().expected));
}

// These macros let us write non-trivial examples without too much text.
#define SUFFIX0  SPV_OPERAND_TYPE_NONE, SPV_OPERAND_TYPE_ID
#define SUFFIX1  SPV_OPERAND_TYPE_ID, SPV_OPERAND_TYPE_SAMPLER_FILTER_MODE, SPV_OPERAND_TYPE_STORAGE_CLASS
INSTANTIATE_TEST_CASE_P(
    OperandPattern, MaskExpansionTest,
    ::testing::ValuesIn(std::vector<MaskExpansionCase>{
        // No bits means no change.
        {SPV_OPERAND_TYPE_OPTIONAL_MEMORY_ACCESS, 0, {SUFFIX0}, {SUFFIX0}},
        // Unknown bits means no change.
        {SPV_OPERAND_TYPE_OPTIONAL_MEMORY_ACCESS,
         0xfffffffc,
         {SUFFIX1},
         {SUFFIX1}},
        // Volatile has no operands.
        {SPV_OPERAND_TYPE_OPTIONAL_MEMORY_ACCESS,
         spv::MemoryAccessVolatileMask,
         {SUFFIX0},
         {SUFFIX0}},
        // Aligned has one literal number operand.
        {SPV_OPERAND_TYPE_OPTIONAL_MEMORY_ACCESS,
         spv::MemoryAccessAlignedMask,
         {SUFFIX1},
         {SPV_OPERAND_TYPE_LITERAL_NUMBER, SUFFIX1}},
        // Volatile with Aligned still has just one literal number operand.
        {SPV_OPERAND_TYPE_OPTIONAL_MEMORY_ACCESS,
         spv::MemoryAccessVolatileMask| spv::MemoryAccessAlignedMask,
         {SUFFIX1},
         {SPV_OPERAND_TYPE_LITERAL_NUMBER, SUFFIX1}},
    }));
#undef SUFFIX0
#undef SUFFIX1

// Returns a vector of all operand types that can be used in a pattern.
std::vector<spv_operand_type_t> allOperandTypes() {
  std::vector<spv_operand_type_t> result;
  for (int i = 0; i < SPV_OPERAND_TYPE_NUM_OPERAND_TYPES; i++) {
    result.push_back(spv_operand_type_t(i));
  }
  return result;
}

using MatchableOperandExpansionTest =
    ::testing::TestWithParam<spv_operand_type_t>;

TEST_P(MatchableOperandExpansionTest, MatchableOperandsDontExpand) {
  const spv_operand_type_t type = GetParam();
  if (!spvOperandIsVariable(type)) {
    spv_operand_pattern_t pattern;
    const bool did_expand = spvExpandOperandSequenceOnce(type, &pattern);
    EXPECT_EQ(false, did_expand);
    EXPECT_THAT(pattern, Eq(spv_operand_pattern_t{}));
  }
}

INSTANTIATE_TEST_CASE_P(MatchableOperandExpansion,
                        MatchableOperandExpansionTest,
                        ::testing::ValuesIn(allOperandTypes()));

using VariableOperandExpansionTest =
    ::testing::TestWithParam<spv_operand_type_t>;

TEST_P(VariableOperandExpansionTest, NonMatchableOperandsExpand) {
  const spv_operand_type_t type = GetParam();
  if (spvOperandIsVariable(type)) {
    spv_operand_pattern_t pattern;
    const bool did_expand = spvExpandOperandSequenceOnce(type, &pattern);
    EXPECT_EQ(true, did_expand);
    EXPECT_FALSE(pattern.empty());
    // For the existing rules, the first expansion of a zero-or-more operand
    // type yields a matchable operand type.  This isn't strictly necessary.
    EXPECT_FALSE(spvOperandIsVariable(pattern.front()));
  }
}

INSTANTIATE_TEST_CASE_P(NonMatchableOperandExpansion,
                        VariableOperandExpansionTest,
                        ::testing::ValuesIn(allOperandTypes()));

}  // anonymous namespace
