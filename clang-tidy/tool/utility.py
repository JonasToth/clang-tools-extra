#!/usr/bin/env python
# -*- coding: utf-8 -*-

from __future__ import print_function

import re

# TODO: `DiagEssence` and `Diagnostic` could maybe be merged into one class
# that provides a 'compress()'-method that calculates the hashes which is
# called before being stored in a `set`. This could result in less code but the
# same memory footprint.


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
    message is found the `DiagEssence` is calculated and deduplicated.
    """

    def __init__(self, path: str, line: int, column: int, diag: str):
        """
        Start initializing this object. The source location is always known
        as it is emitted first and always in a single line.
        `diag` will contain all warning/error/note information until the first
        line-break. These are very uncommon, but CSA's PaddingChecker
        emits a multi-line warning containing the optimal layout of a record.
        These additional lines must be added after creation of the
        `Diagnostic`.
        """
        self._path = path
        self._line = line
        self._column = column
        self._diag = diag
        self._additional = ""

    def add_additional_line(self, line: str):
        """Store more additional information line per line while parsing."""
        self._additional += "\n" + line

    def get_essence(self):
        """Return an object `DiagEssence` from this instance."""
        return DiagEssence(self._path, self._line, self._column, self._diag,
                           self._additional)

    def __str__(self):
        """Transform the object back into a raw diagnostic."""
        return self._path + ":" + str(self._line) + ":" + str(self._column)\
                          + ": " + self._diag + self._additional


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


def _is_valid_diag_match(match_groups):
    return all(g is not None for g in match_groups)


def _diag_from_match(match_groups):
    return Diagnostic(
        str(match_groups[0]), int(match_groups[1]), int(match_groups[2]),
        str(match_groups[3]) + ": " + str(match_groups[4]))


class ParseClangTidyDiagnostics(object):
    """
    This class is a stateful parser for `clang-tidy` diagnostic output.
    The parser collects all unique diagnostics that can be emitted after
    deduplication.
    """

    def __init__(self):
        super(ParseClangTidyDiagnostics, self).__init__()
        self._diag_re = re.compile(
            r"^(.+):(\d+):(\d+): (error|warning): (.*)$")
        self._current_diag = None

        self._dedup = Deduplication()
        self._uniq_diags = list()

    def reset_parser(self):
        """
        Clean the parsing data to prepare for another set of output from
        `clang-tidy`. The deduplication is not cleaned because that data
        is required between multiple parsing runs. The diagnostics are cleaned
        as the parser assumes the new unique diagnostics are consumed before
        the parser is reset.
        """
        self._current_diag = None
        self._uniq_diags = list()

    def get_diags(self):
        """
        Returns a list of diagnostics that can be emitted after parsing the
        full output of a clang-tidy invocation.
        The list contains no duplicates.
        """
        return self._uniq_diags

    def parse_string(self, input_str):
        """Parse a string like captured stdout."""
        self._parse_lines(input_str.splitlines())

    def _parse_line(self, line):
        """Parses on line into internal state, returns nothing."""
        match = self._diag_re.match(line)

        # A new diagnostic is found (either error or warning).
        if match and _is_valid_diag_match(match.groups()):
            self._handle_new_diag(match.groups())

        # There was no new diagnostic but a previous diagnostic is in flight.
        # Interpret this situation as additional output like notes or
        # code-pointers from the diagnostic that is in flight.
        elif not match and self._current_diag:
            self._current_diag.add_additional_line(line)

        # There was no diagnostic in flight and this line did not create a
        # new one. This situation should not occur, but might happen if
        # clang-tidy emits information before warnings start.
        else:
            return

    def _handle_new_diag(self, match_groups):
        # The current in-flight diagnostic was not emitted before, therefor
        # it should be stored as a new unique diagnostic.
        self._register_diag()
        self._current_diag = _diag_from_match(match_groups)

    def _register_diag(self):
        if self._current_diag and \
           self._dedup.insert_and_query(self._current_diag.get_essence()):
            self._uniq_diags.append(self._current_diag)

    def _parse_lines(self, line_list):
        assert self._current_diag is None, \
               "Parser not in a clean state to restart parsing"
        for line in line_list:
            self._parse_line(line.rstrip())
        # Register the last diagnostic after all input is parsed.
        self._register_diag()

    def _parse_file(self, filename):
        with open(filename, "r") as input_file:
            self._parse_lines(input_file.readlines())
