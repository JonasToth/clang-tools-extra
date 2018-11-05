#!/usr/bin/env python
# -*- coding: utf-8 -*-

from __future__ import print_function

import logging as log
import re
import sys

log.basicConfig(level=log.INFO)


class CheckDiagnostic(object):
    """
    This class represents a single emitted diagnostic and is used to store
    all emitted warnings/notes in a structured manner.
    """

    def __init__(self, severity, location, diagnostic, check_name=""):
        self.__severity = severity
        self.__location = location
        self.__diagnostic = diagnostic
        self.__name = check_name
        self.__code_hints = ""

        self.__description = None
        self.__full_text = None
        self.__full_text_dirty = True

    def add_code_hints(self, line):
        self.__code_hints += line + "\n"
        self.__full_text_dirty = True

    def get_severity(self):
        return self.__severity

    def get_name(self):
        return self.__name

    def get_location(self):
        return self.__location

    def get_diagnostic(self):
        return self.__diagnostic

    def get_code_hints(self):
        return self.__code_hints

    def get_description(self):
        if self.__description is None:
            self.__description = self.__location + ": " + self.__severity + \
                                 ": " + self.__diagnostic + " [" + \
                                 self.__name + "]"
        return self.__description

    def get_full_text(self):
        if self.__full_text_dirty:
            self.__full_text = self.get_description() + "\n" + \
                               self.__code_hints
            self.__full_text_dirty = False
        return self.__full_text

    def __hash__(self):
        return self.get_full_text().__hash__()


class FindingDiagnostic(object):
    """
    This class represents all diagnostics for a specific finding.
    It is a ORDERED list of all diagnostics emitted in the original order.
    """

    def __init__(self, initial_diagnostic):
        self.__diagnostics = [
            initial_diagnostic,
        ]
        self.__str_representation = None
        self.__str_representation_dirtry = True

    def add_diagnostic(self, diag):
        self.__diagnostics.append(diag)
        self.__str_representation_dirtry = True

    def get_diagnostics(self):
        return self.__diagnostics


    def __lt__(self, other):
        return self.get_diagnostics()[0].get_location() < \
               other.get_diagnostics()[0].get_location()

    def __eq__(self, other):
        return self.__str__() == other.__str__()

    def __str__(self):
        if self.__str_representation_dirtry:
            self.__str_representation = "".join([d.get_full_text() \
                                                 for d in self.__diagnostics])
            self.__str_representation_dirtry = False
        return self.__str_representation

    def __unicode__(self):
        return self.__str__()

    def __hash__(self):
        return self.__str__().__hash__()


class Findings(object):
    """
    This class is a container for all findings inserted here.
    It does the deduplication.
    """

    def __init__(self):
        self.__total_warnings_count = dict()
        self.__uniq_warnings = dict()

    def register_finding(self, check_name, finding):
        if check_name not in self.__total_warnings_count:
            assert check_name not in self.__uniq_warnings
            self.__total_warnings_count[check_name] = 0
            self.__uniq_warnings[check_name] = set()

        self.__total_warnings_count[check_name] += 1
        self.__uniq_warnings[check_name].add(finding)

    def all_diagnostics(self, check_name):
        return sorted(list(self.findings_for_check(check_name)))

    def finding_checks(self):
        return self.__uniq_warnings.keys()

    def findings_for_check(self, check_name):
        return self.__uniq_warnings[check_name]

    def total_finding_count(self, check_name=None):
        if check_name:
            return self.__total_warnings_count.get(check_name, 0)
        return sum([n for _, n in self.__total_warnings_count.items()])

    def unique_finding_count(self, check_name=None):
        if check_name:
            return len(self.__uniq_warnings[check_name]) \
                       if check_name in self.__uniq_warnings else 0
        return sum([len(n) for _, n in self.__uniq_warnings.items()])


class ParseBBOutput(object):
    """
    This class parses the full buildbot output and extract the diagnostics
    for each enabled check.
    It deduplicates the diagnostics as well.
    """

    def __init__(self):
        super(ParseBBOutput, self).__init__()

        self.__diagnostic_regex = re.compile(
            r"^(.+): (error|warning|note): ([^\[]+) ?(\[([^,]+).*\])?$")
        self.__known_ct_noise_output = re.compile(
            r"^(Suppressed|Use -header-filter|clang-apply|\d+ warnings|$)")

        # State of the parser to control what to look for next
        # At the beginning, run-clang-tidy.py writes the list of enabled
        # checks.
        self.__expect_enabled_checks = True
        self.__expect_check_name = False

        # Then it prints diagnostics followed from one or multiple lines
        # of pointers into the code and/or fix-it's (ignored now).
        self.__expect_warnings = False
        self.__current_finding_check = None
        self.__current_finding = None

        # List of enabled checks.
        self.__enabled_checks = list()
        self.__all_findings = Findings()

    def parse_file(self, filename):
        with open(filename, "r") as input_file:
            lines = list(map(lambda l: l.rstrip(), input_file.readlines()))

        self.parse_lines(lines)

    def parse_string(self, input):
        self.parse_lines(list(map(lambda l: l.rstrip(), input.splitlines())))

    def parse_lines(self, line_list):
        assert self.__expect_enabled_checks, "Not in start state"
        for line in line_list:
            self.parse_line(line)

    def parse_line(self, line):
        """
        Returns True if it expects more output, otherwise False.
        """
        log.debug("New line: %s", line)
        if self.__expect_enabled_checks and line == "Enabled checks:":
            log.debug("Found start of Enabled checks:")
            self.__expect_enabled_checks = False
            self.__expect_check_name = True
            return True

        if self.__expect_check_name:
            if line:
                log.debug("Enabled check found: %s", line.strip())
                self.__enabled_checks.append(line.strip())
            else:
                self.__expect_check_name = False
                self.__expect_warnings = True
                log.debug("Read in all enabled checks, expect warnings")
            return True

        if line.startswith("Applying fixes ..."):
            return False

        if self.__expect_warnings:
            potential_match = self.__diagnostic_regex.match(line)
            g = (None, None, None, None, None)
            if potential_match:
                g = potential_match.groups()
            (severity, location, msg, category_tag, category) = g

            if potential_match:
                log.debug("Found a direct diagnostic message:")
                log.debug("%s; %s; %s", severity, location, msg)

                diag = None
                # Warning of the following form found:
                # /location.h:6:13: warning: blaa [hicpp-dont]
                if category_tag and severity and location and msg:
                    assert category, "Tag found, category to be extracted"
                    # should be a warning/error, as it contains a category
                    diag = CheckDiagnostic(severity, location, msg, category)

                    # It might be a new warning. In this case the previous
                    # warning must be registered, as there is no
                    # additional information to it.
                    if self.__current_finding:
                        log.debug("Registering current finding: %s",
                                  self.__current_finding_check)
                        assert self.__current_finding_check, \
                            "Finding needs category"
                        # insert the previous diagnostic first
                        self.__all_findings.register_finding(
                            self.__current_finding_check,
                            self.__current_finding)
                        self.__current_finding_check = None
                        self.__current_finding = None

                    log.debug("Start new Finding: %s", category)
                    # Store the finding as the current finding, as the
                    # potential old finding is already inserted in all
                    # findings.
                    self.__current_finding = FindingDiagnostic(diag)
                    self.__current_finding_check = category

                # Message in the following form found
                # /location.h:6:13: note: blaa
                elif severity and location and msg:
                    log.debug("Found note-like diag")
                    if self.__current_finding:
                        diag = CheckDiagnostic(severity, location, msg)
                        log.debug("Adding more to finding: %s", line)
                        self.__current_finding.add_diagnostic(diag)
                # Spurious match
                else:
                    pass
                return True

            if self.__known_ct_noise_output.match(line):
                # Noise, that clang-tidy outputs (e.g. how many warnings
                # were generated).
                # It could be the breaking condition, that's why there is
                # no continuation. But the line itself is ignored here.
                log.debug("Me Orc, Me Spam, No Mod, No Ban")

                if self.__current_finding:
                    assert self.__current_finding_check, \
                           "Check-name required"
                    log.debug("Register finding before ignoring, %s",
                              self.__current_finding_check)
                    self.__all_findings.register_finding(
                        self.__current_finding_check, self.__current_finding)
                    self.__current_finding = None
                    self.__current_finding_check = None
                return True

            # Found a code-hint that can be added to the current
            # Diagnostic record.
            if self.__current_finding:
                log.debug("Adding code-hint to diag: %s", line)
                self.__current_finding.get_diagnostics()[-1].add_code_hints(
                    line)
            return True
        return True

    def get_enabled_checks(self):
        return self.__enabled_checks

    def get_findings(self):
        return self.__all_findings
