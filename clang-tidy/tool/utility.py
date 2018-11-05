#!/usr/bin/env python
# -*- coding: utf-8 -*-

from __future__ import print_function

import logging as log
import re
import sys

log.basicConfig(level=log.INFO)

# TODO: `DiagEssence` and `Diagnostic` could be merged into one class that
# provides a 'compress()'-method that calculates the hashes which is called
# before being stored in a `set`. This could result in less code but the same
# memory footprint. Take a look at it!

class DiagEssence(object):
    """
    This class stores only the essential information to distinguish diagnostics.
    Having only minimal information reduces the amount data to store for each
    diagnostic and avoid storing its complete string.
    This size-optimization is required to minimize the memory footprint of the
    deduplication effort as big runs of `run-clang-tidy.py` (e.g. on LLVM)
    can produce ~GB of diagnostic without deduplication. Storing full 
    diagnostics requires two orders of magnitude less for the worst observed
    configuration (the worst checks produced their diagnostics about 100 times
    in real world configurations).
    As the average case is more one order of magnitude less data deduplication
    requires still ~100MB plus data structure overhead, which is too much.

    Please note that the fingerprinting is done with pythons builtin __hash__()
    function which is not cryptographically secure. All informations are
    reduced to integers and stored. The chance that *ALL* these integers match
    at the same time (line and column are not changed!) is > 0
    but for non-malicious input almost impossible.
    """

    def __init__(self, path: str, line: int, column: int, diag: str,
                 additional_info: str):
        """
        A fingerprint of the diagnostic information is stored to allow
        deduplication in a `set` data structure.

        :param path: file-path the diagnostic is emitted from
        :param line: line-number of the diagnostic location
        :param column: column number of the diagnostic location
        :param diag: first line of the diagnostic message, usually a warning
        :param additional_info: extra notes, code-points or other information
        """
        super(DiagEssence, self).__init__()
        self._path = path.__hash__()
        self._line = line
        self._column = column
        self._hash = (self._path + self._line + self._column).__hash__()

        self._diag = diag.__hash__()
        self._additional = additional_info.__hash__()

    def __hash__(self):
        """
        Return the precalculated hash from the constructor.
        The hash of this object only consists of information to the position
        of the diagnostic, as most diagnotics have distinct source locations.
        """
        return self._hash

    def __eq__(self, other):
        """
        Determine if two diagnostics are identical. The comparison takes all
        aspects of the diagnostic fingerprint into account and might be slower
        then hash comparison.
        As this class is assumed to be used only in a `set` data structure
        the equality comparison does *NOT* compare the `_hash` attribute of
        both objects as the equality check happens after the hash comparison.
        The source location is still considered for equality!
        
        Comparison order:
            - line number (int)
            - column number (int)
            - path-hash (int, hashed from the file-path)
            - diag (int, hashed from the first diagnostic line)
            - additional (int, hashed from additional information, e.g. notes)
        """
        if self._line != other._line:
            return False
        if self._column != other._column:
            return False
        if self._path != other._path:
            return False
        if self._diag != other._diag:
            return False
        if self._additional != other._additional:
            return False
        return True


class Diagnostic(object):
    """
    This class represents a parsed diagnostic message coming from clang-tidy
    output. While parsing the raw output each new diagnostic will incrementally
    build a temporary object of this class. Once the end of the diagnotic
    message is found the `DiagEssence` is calculated and deduplicated. If
    there was no equivalent `DiagEssence` stored before the diagnostic is
    emitted to `sys.stdout` and the `DiagEssence` is stored.
    """

    def __init__(self, path: str, line: int, column: int, diag: str):
        """
        Start initializing this object. The source location is always known
        as it is emitted first and always in a single line.
        `diag` will contain all warning/error/note information until the first
        line-break. These are very uncommon, but CSA's PaddingChecker
        emits a multi-line warning containing the optimal layout of a record.
        These additional lines must be added after creation of the `Diagnostic`.
        """
        self._path = path
        self._line = line
        self._column = column
        self._diag = diag
        self._additional = ""

    def add_additional_line(self, line: str):
        """Store more additional information line per line while parsing."""
        self._additional += line

    def get_essence(self):
        """Return an object `DiagEssence` from this instance."""
        return DiagEssence(self._path, self._line, self._column, self._diag,
                           self._additional)


class Deduplication(object):
    """
    This class provides an interface to deduplicate diagnostics emitted from
    `clang-tidy`. It maintains a `set` of `DiagEssence` objects and allows
    to query if an diagnostic is already emitted (according to a corresponding
    `DiagEssence` object!).
    """

    def __init__(self):
        """Initializes and empty set."""
        self._set = set()

    def insert_and_query(self, diag: DiagEssence):
        """
        This method returns True if the `diag` was *NOT* emitted already
        signaling that the parser shall store/emit this diagnostic.
        Otherwise (if the `diag` was stored already) it return False and has
        no effect.
        """
        if diag not in self._set:
            self._set.add(diag)
            return True
        return False


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
