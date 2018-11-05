#!/usr/bin/env python
# -*- coding: utf-8 -*-

from utility import Diagnostic, DiagEssence, Deduplication
from utility import ParseBBOutput
import unittest


class TestDiagnostics(unittest.TestCase):
    """Test fingerprinting diagnostic messages"""

    def test_construction(self):
        d = Diagnostic("/home/user/project/my_file.h", 24, 4,
                        "warning: Do not do this thing [warning-category]")
        self.assertIsNotNone(d)

        de = DiagEssence("/home/user/project/my_file.h", 24, 4,
                         "warning: Do not do this thing [warning-category]",
                         "      MyCodePiece();\n"\
                         "      ^\n")
        self.assertIsNotNone(de)
        
        d.add_additional_line("      MyCodePiece();\n")
        d.add_additional_line("      ^\n")

        self.assertEqual(d.get_essence().__hash__(), de.__hash__())
        self.assertEqual(d.get_essence(), de)

    def test_comparisons(self):
        de1 = DiagEssence("/home/user/project/my_file.h", 24, 4,
                          "warning: Do not do this thing [warning-category]",
                          "      MyCodePiece();\n"\
                          "      ^\n")
        de2 = DiagEssence("/home/user/project/my_file.h", 24, 4,
                          "warning: Do not do this thing [warning-category]",
                          "      MyCodePiece();\n"\
                          "      ^\n")
        self.assertEqual(de1.__hash__(), de2.__hash__())
        self.assertEqual(de1, de2)

        # Changing location information must influence the hash.
        de3 = DiagEssence("/home/user/project/my_file.h", 24, 5,
                          "warning: Do not do this thing [warning-category]",
                          "      MyCodePiece();\n"\
                          "      ^\n")
        self.assertNotEqual(de1.__hash__(), de3.__hash__())
        self.assertNotEqual(de1, de3)

        de4 = DiagEssence("/home/user/project/my_file.h", 1, 4,
                          "warning: Do not do this thing [warning-category]",
                          "      MyCodePiece();\n"\
                          "      ^\n")
        self.assertNotEqual(de1.__hash__(), de4.__hash__())
        self.assertNotEqual(de1, de4)

        de5 = DiagEssence("/home/user/project/another_file.h", 24, 4,
                          "warning: Do not do this thing [warning-category]",
                          "      MyCodePiece();\n"\
                          "      ^\n")
        self.assertNotEqual(de1.__hash__(), de5.__hash__())
        self.assertNotEqual(de1, de5)

        # Keeping the location the same but changing the diagnostic messages
        # Does *NOT* have an influence on the hash.
        de6 = DiagEssence("/home/user/project/my_file.h", 24, 4,
                          "warning: Do not do this thing [other-category]",
                          "      MyCodePiece();\n"\
                          "      ^\n")
        self.assertEqual(de1.__hash__(), de6.__hash__())
        self.assertNotEqual(de1, de6)

        de7 = DiagEssence("/home/user/project/my_file.h", 24, 4,
                          "warning: Different message [warning-category]",
                          "      MyCodePiece();\n"\
                          "      ^\n")
        self.assertEqual(de1.__hash__(), de7.__hash__())
        self.assertNotEqual(de1, de7)

        de8 = DiagEssence("/home/user/project/my_file.h", 24, 4,
                          "warning: Do not do this thing [warning-category]",
                          "      MyCodePiece<Template>();\n")
        self.assertEqual(de1.__hash__(), de8.__hash__())
        self.assertNotEqual(de1, de8)

        de9 = DiagEssence("/home/user/project/my_file.h", 24, 4,
                          "warning: Do not do this thing [warning-category]",
                          "")
        self.assertEqual(de1.__hash__(), de9.__hash__())
        self.assertNotEqual(de1, de8)


class TestDeduplication(unittest.TestCase):
    """Test the `DiagEssence` based deduplication of diagnostic messages."""
    
    def test_construction(self):
        self.assertIsNotNone(Deduplication())

    def test_dedup(self):
        dedup = Deduplication()

        de1 = DiagEssence("/home/user/project/my_file.h", 24, 4,
                          "warning: Do not do this thing [warning-category]",
                          "      MyCodePiece();\n"\
                          "      ^\n")
        self.assertTrue(dedup.insert_and_query(de1))
        self.assertFalse(dedup.insert_and_query(de1))

        # Create a diagnostic with the same hash that is not equal.
        de2 = DiagEssence("/home/user/project/my_file.h", 24, 4,
                          "warning: Do not do this thing [mess-with-equality]",
                          "      MyCodePiece();\n"\
                          "      ^\n")
        self.assertTrue(dedup.insert_and_query(de2))
        self.assertFalse(dedup.insert_and_query(de2))

        # Create a diagnostic with a different hash and insert it.
        de3 = DiagEssence("/home/user/project/mess_with_hash.h", 24, 5,
                          "warning: Do not do this thing [mess-with-equality]",
                          "      MyCodePiece();\n"\
                          "      ^\n")
        self.assertTrue(dedup.insert_and_query(de3))
        self.assertFalse(dedup.insert_and_query(de3))

    
class TestLinewiseParsing(unittest.TestCase):
    @unittest.skip
    def test_performance_output(self):
        enabled_checks = [
            "performance-faster-string-find",
            "performance-for-range-copy",
            "performance-implicit-conversion-in-loop",
            "performance-inefficient-algorithm",
            "performance-inefficient-string-concatenation",
            "performance-inefficient-vector-operation",
            "performance-move-const-arg",
            "performance-move-constructor-init",
            "performance-noexcept-move-constructor",
            "performance-type-promotion-in-math-fn",
            "performance-unnecessary-copy-initialization",
            "performance-unnecessary-value-param",
        ]
        performance_result = ParseBBOutput()
        performance_result.parse_file("test_input/out_performance_cmake.log")

        self.assertListEqual(performance_result.get_enabled_checks(),
                             enabled_checks)

    @unittest.skip
    def test_hicpp_output(self):
        enabled_checks = [
            "hicpp-avoid-goto",
            "hicpp-braces-around-statements",
            "hicpp-deprecated-headers",
            "hicpp-exception-baseclass",
            "hicpp-explicit-conversions",
            "hicpp-function-size",
            "hicpp-invalid-access-moved",
            "hicpp-member-init",
            "hicpp-move-const-arg",
            "hicpp-multiway-paths-covered",
            "hicpp-named-parameter",
            "hicpp-new-delete-operators",
            "hicpp-no-array-decay",
            "hicpp-no-assembler",
            "hicpp-no-malloc",
            "hicpp-noexcept-move",
            "hicpp-signed-bitwise",
            "hicpp-special-member-functions",
            "hicpp-static-assert",
            "hicpp-undelegated-constructor",
            "hicpp-use-auto",
            "hicpp-use-emplace",
            "hicpp-use-equals-default",
            "hicpp-use-equals-delete",
            "hicpp-use-noexcept",
            "hicpp-use-nullptr",
            "hicpp-use-override",
            "hicpp-vararg",
        ]
        hicpp_result = ParseBBOutput()
        hicpp_result.parse_file("test_input/out_hicpp_cmake.log")

        self.assertListEqual(hicpp_result.get_enabled_checks(), enabled_checks)

        f = hicpp_result.get_findings()
        n_warnings = 45407
        self.assertEqual(f.total_finding_count(), n_warnings)

        uniq_n_goto = 108
        self.assertEqual(
            f.unique_finding_count("hicpp-avoid-goto"), uniq_n_goto)
        uniq_n_auto = 745
        self.assertEqual(f.unique_finding_count("hicpp-use-auto"), uniq_n_auto)
        all_n_auto = 878
        self.assertEqual(f.total_finding_count("hicpp-use-auto"), all_n_auto)

    def test_visual_inspection(self):
        pass
        # print(f.finding_checks())
        # for diag in f.findings_for_check(self.result.get_enabled_checks()[0]):
        # print(diag)

        # for check in f.finding_checks():
        # print("{}: {} unique findings".format(
        # check, len(f.findings_for_check(check))))

        # for (check, findings) in f.raw_results().items():
        # print("{}:  {} raw findings".format(check, len(findings)))


if __name__ == "__main__":
    unittest.main()
