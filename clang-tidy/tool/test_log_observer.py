#!/usr/bin/env python
# -*- coding: utf-8 -*-

from utility import CheckDiagnostic, FindingDiagnostic, Findings, ParseBBOutput
import unittest


class TestDiagnostic(unittest.TestCase):
    def test_basic_properties(self):
        d1 = CheckDiagnostic("warning", "/file.h:62:14", "don't do it",
                             "bugprone-use-after-move")
        d2 = CheckDiagnostic("warning", "/file.h:65:10", "don't do it",
                             "bugprone-use-after-move")

        self.assertEqual(d1.get_severity(), "warning")
        self.assertEqual(d2.get_severity(), "warning")

        self.assertEqual(d1.get_location(), "/file.h:62:14")
        self.assertEqual(d2.get_location(), "/file.h:65:10")

        self.assertEqual(d1.get_diagnostic(), "don't do it")
        self.assertEqual(d2.get_diagnostic(), "don't do it")

        self.assertEqual(d1.get_name(), "bugprone-use-after-move")
        self.assertEqual(d2.get_name(), "bugprone-use-after-move")

        self.assertEqual(d1.get_code_hints(), "")
        self.assertEqual(d2.get_code_hints(), "")

        self.assertEqual(
            d1.get_description(),
            "/file.h:62:14: warning: don't do it [bugprone-use-after-move]")
        self.assertEqual(
            d2.get_description(),
            "/file.h:65:10: warning: don't do it [bugprone-use-after-move]")

        self.assertEqual(d1.get_description() + "\n", d1.get_full_text())
        self.assertEqual(d2.get_description() + "\n", d2.get_full_text())

        self.assertNotEqual(d1.__hash__(), d2.__hash__())

    def test_code_hints(self):
        d = CheckDiagnostic("warning", "bugprone-use-after-move",
                            "/file.h:62:14", "don't do it")
        d.add_code_hints("     std::move(some_variable)")
        d.add_code_hints("               ^")

        self.assertEqual(d.get_code_hints(),
                         "     std::move(some_variable)\n               ^\n")
        self.assertNotEqual(d.get_description() + "\n", d.get_full_text())


class TestFindingDiagnostic(unittest.TestCase):
    def test_single(self):
        d = CheckDiagnostic("warning", "/file.h:62:14", "don't do it",
                            "bugprone-use-after-move")
        w = CheckDiagnostic("note", "/file.h:60:3", "moved here")

        f = FindingDiagnostic(d)
        f.add_diagnostic(w)

        self.assertEqual(f.get_diagnostics()[0].__hash__(), d.__hash__())
        self.assertEqual(f.get_diagnostics()[1].__hash__(), w.__hash__())

    def test_set_ability(self):
        w = CheckDiagnostic("warning", "/file.h:62:14", "don't do it",
                            "bugprone-use-after-move")
        n = CheckDiagnostic("note", "/file.h:60:3", "moved here")

        f1 = FindingDiagnostic(w)
        f1.add_diagnostic(n)

        f2 = FindingDiagnostic(n)
        f2.add_diagnostic(w)

        self.assertNotEqual(f1, f2)
        self.assertNotEqual(f1.__hash__(), f2.__hash__())

        f3 = FindingDiagnostic(w)
        f3.add_diagnostic(n)

        self.assertEqual(f1.__hash__(), f3.__hash__())
        self.assertEqual(f1, f3)

    def test_le_comparison(self):
        w1 = CheckDiagnostic("warning", "/file.h:62:14", "don't do it",
                             "bugprone-use-after-move")
        n1 = CheckDiagnostic("note", "/file.h:60:3", "moved here")
        f1 = FindingDiagnostic(w1)
        f1.add_diagnostic(n1)

        self.assertFalse(f1 < f1)
        self.assertTrue(f1 == f1)

        w2 = CheckDiagnostic("warning", "/file.h:62:17", "don't do it",
                            "bugprone-use-after-move")
        n2 = CheckDiagnostic("note", "/file.h:60:3", "moved here")
        f2 = FindingDiagnostic(w2)
        f2.add_diagnostic(n2)

        self.assertTrue(f1 < f2)
        self.assertFalse(f1 == f2)



class TestFindings(unittest.TestCase):
    def setUp(self):
        d = CheckDiagnostic("warning", "/file.h:62:14", "don't do it",
                            "bugprone-use-after-move")
        w = CheckDiagnostic("note", "/file.h:60:3", "moved here")

        self.f = FindingDiagnostic(d)
        self.f.add_diagnostic(w)

        self.findings = Findings()
        self.findings.register_finding("bugprone-use-after-move", self.f)
        self.findings.register_finding("bugprone-use-after-move", self.f)

    def test_addition(self):
        self.assertEqual(len(self.findings.finding_checks()), 1)
        self.assertEqual(
            list(self.findings.finding_checks())[0], "bugprone-use-after-move")

        self.assertEqual(self.findings.total_finding_count(), 2)
        self.assertEqual(self.findings.unique_finding_count(), 1)
        self.assertTrue(
            self.f in self.findings.findings_for_check(
                "bugprone-use-after-move"))

    def test_counts(self):
        self.assertEqual(self.findings.total_finding_count(), 2)
        self.assertEqual(self.findings.unique_finding_count(), 1)

        self.assertEqual(
            self.findings.total_finding_count("bugprone-use-after-move"), 2)
        self.assertEqual(
            self.findings.total_finding_count("bugprone-no-existing"), 0)

        self.assertEqual(
            self.findings.unique_finding_count("bugprone-use-after-move"), 1)
        self.assertEqual(
            self.findings.unique_finding_count("bugprone-no-existing"), 0)


class TestLinewiseParsing(unittest.TestCase):
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
