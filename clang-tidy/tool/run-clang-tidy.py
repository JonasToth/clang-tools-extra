#!/usr/bin/env python
#
#===- run-clang-tidy.py - Parallel clang-tidy runner ---------*- python -*--===#
#
#                     The LLVM Compiler Infrastructure
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#
# FIXME: Integrate with clang-tidy-diff.py

"""
Parallel clang-tidy runner
==========================

Runs clang-tidy over all files in a compilation database. Requires clang-tidy
and clang-apply-replacements in $PATH.

Example invocations.
- Run clang-tidy on all files in the current working directory with a default
  set of checks and show warnings in the cpp files and all project headers.
    run-clang-tidy.py $PWD

- Fix all header guards.
    run-clang-tidy.py -fix -checks=-*,llvm-header-guard

- Fix all header guards included from clang-tidy and header guards
  for clang-tidy headers.
    run-clang-tidy.py -fix -checks=-*,llvm-header-guard extra/clang-tidy \
                      -header-filter=extra/clang-tidy

Compilation database setup:
http://clang.llvm.org/docs/HowToSetupToolingForLLVM.html
"""

from __future__ import print_function

import argparse
import glob
import json
import multiprocessing
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import traceback
import yaml

is_py2 = sys.version[0] == '2'

if is_py2:
    import Queue as queue
else:
    import queue as queue

def find_compilation_database(path):
  """Adjusts the directory until a compilation database is found."""
  result = './'
  while not os.path.isfile(os.path.join(result, path)):
    if os.path.realpath(result) == '/':
      print('Error: could not find compilation database.')
      sys.exit(1)
    result += '../'
  return os.path.realpath(result)


def make_absolute(f, directory):
  if os.path.isabs(f):
    return f
  return os.path.normpath(os.path.join(directory, f))


def get_tidy_invocation(f, clang_tidy_binary, checks, tmpdir, build_path,
                        header_filter, extra_arg, extra_arg_before, quiet,
                        config):
  """Gets a command line for clang-tidy."""
  start = [clang_tidy_binary]
  if header_filter is not None:
    start.append('-header-filter=' + header_filter)
  else:
    # Show warnings in all in-project headers by default.
    start.append('-header-filter=^' + build_path + '/.*')
  if checks:
    start.append('-checks=' + checks)
  if tmpdir is not None:
    start.append('-export-fixes')
    # Get a temporary file. We immediately close the handle so clang-tidy can
    # overwrite it.
    (handle, name) = tempfile.mkstemp(suffix='.yaml', dir=tmpdir)
    os.close(handle)
    start.append(name)
  for arg in extra_arg:
      start.append('-extra-arg=%s' % arg)
  for arg in extra_arg_before:
      start.append('-extra-arg-before=%s' % arg)
  start.append('-p=' + build_path)
  if quiet:
      start.append('-quiet')
  if config:
      start.append('-config=' + config)
  start.append(f)
  return start


def merge_replacement_files(tmpdir, mergefile):
  """Merge all replacement files in a directory into a single file"""
  # The fixes suggested by clang-tidy >= 4.0.0 are given under
  # the top level key 'Diagnostics' in the output yaml files
  mergekey="Diagnostics"
  merged=[]
  for replacefile in glob.iglob(os.path.join(tmpdir, '*.yaml')):
    content = yaml.safe_load(open(replacefile, 'r'))
    if not content:
      continue # Skip empty files.
    merged.extend(content.get(mergekey, []))

  if merged:
    # MainSourceFile: The key is required by the definition inside
    # include/clang/Tooling/ReplacementsYaml.h, but the value
    # is actually never used inside clang-apply-replacements,
    # so we set it to '' here.
    output = { 'MainSourceFile': '', mergekey: merged }
    with open(mergefile, 'w') as out:
      yaml.safe_dump(output, out)
  else:
    # Empty the file:
    open(mergefile, 'w').close()


def check_clang_apply_replacements_binary(args):
  """Checks if invoking supplied clang-apply-replacements binary works."""
  try:
    subprocess.check_call([args.clang_apply_replacements_binary, '--version'])
  except:
    print('Unable to run clang-apply-replacements. Is clang-apply-replacements '
          'binary correctly specified?', file=sys.stderr)
    traceback.print_exc()
    sys.exit(1)


def apply_fixes(args, tmpdir):
  """Calls clang-apply-fixes on a given directory."""
  invocation = [args.clang_apply_replacements_binary]
  if args.format:
    invocation.append('-format')
  if args.style:
    invocation.append('-style=' + args.style)
  invocation.append(tmpdir)
  subprocess.call(invocation)

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

    def __init__(self, path, line, column, diag, additional_info):
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

    def __init__(self, path, line, column, diag):
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

    def add_additional_line(self, line):
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

    def insert_and_query(self, diag):
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


def run_tidy(args, tmpdir, build_path, queue, lock, failed_files):
  """Takes filenames out of queue and runs clang-tidy on them."""
  parser = ParseClangTidyDiagnostics()
  while True:
    name = queue.get()
    invocation = get_tidy_invocation(name, args.clang_tidy_binary, args.checks,
                                     tmpdir, build_path, args.header_filter,
                                     args.extra_arg, args.extra_arg_before,
                                     args.quiet, args.config)

    proc = subprocess.Popen(invocation, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    output, err = proc.communicate()
    if proc.returncode != 0:
      failed_files.append(name)

    with lock:
      parser.parse_string(output.decode('utf-8'))
      sys.stdout.write(' '.join(invocation) + '\n' +\
                       "\n".join([str(diag) for diag in parser.get_diags()])\
                       + '\n')
      parser.reset_parser()

      if len(err) > 0:
        sys.stderr.write(err.decode('utf-8') + '\n')

    queue.task_done()


def main():
  parser = argparse.ArgumentParser(description='Runs clang-tidy over all files '
                                   'in a compilation database. Requires '
                                   'clang-tidy and clang-apply-replacements in '
                                   '$PATH.')
  parser.add_argument('-clang-tidy-binary', metavar='PATH',
                      default='clang-tidy',
                      help='path to clang-tidy binary')
  parser.add_argument('-clang-apply-replacements-binary', metavar='PATH',
                      default='clang-apply-replacements',
                      help='path to clang-apply-replacements binary')
  parser.add_argument('-checks', default=None,
                      help='checks filter, when not specified, use clang-tidy '
                      'default')
  parser.add_argument('-config', default=None,
                      help='Specifies a configuration in YAML/JSON format: '
                      '  -config="{Checks: \'*\', '
                      '                       CheckOptions: [{key: x, '
                      '                                       value: y}]}" '
                      'When the value is empty, clang-tidy will '
                      'attempt to find a file named .clang-tidy for '
                      'each source file in its parent directories.')
  parser.add_argument('-header-filter', default=None,
                      help='regular expression matching the names of the '
                      'headers to output diagnostics from. Diagnostics from '
                      'the main file of each translation unit are always '
                      'displayed.')
  parser.add_argument('-export-fixes', metavar='filename', dest='export_fixes',
                      help='Create a yaml file to store suggested fixes in, '
                      'which can be applied with clang-apply-replacements.')
  parser.add_argument('-j', type=int, default=0,
                      help='number of tidy instances to be run in parallel.')
  parser.add_argument('files', nargs='*', default=['.*'],
                      help='files to be processed (regex on path)')
  parser.add_argument('-fix', action='store_true', help='apply fix-its')
  parser.add_argument('-format', action='store_true', help='Reformat code '
                      'after applying fixes')
  parser.add_argument('-style', default='file', help='The style of reformat '
                      'code after applying fixes')
  parser.add_argument('-p', dest='build_path',
                      help='Path used to read a compile command database.')
  parser.add_argument('-extra-arg', dest='extra_arg',
                      action='append', default=[],
                      help='Additional argument to append to the compiler '
                      'command line.')
  parser.add_argument('-extra-arg-before', dest='extra_arg_before',
                      action='append', default=[],
                      help='Additional argument to prepend to the compiler '
                      'command line.')
  parser.add_argument('-quiet', action='store_true',
                      help='Run clang-tidy in quiet mode')
  args = parser.parse_args()

  db_path = 'compile_commands.json'

  if args.build_path is not None:
    build_path = args.build_path
  else:
    # Find our database
    build_path = find_compilation_database(db_path)

  try:
    invocation = [args.clang_tidy_binary, '-list-checks']
    invocation.append('-p=' + build_path)
    if args.checks:
      invocation.append('-checks=' + args.checks)
    invocation.append('-')
    subprocess.check_call(invocation)
  except:
    print("Unable to run clang-tidy.", file=sys.stderr)
    sys.exit(1)

  # Load the database and extract all files.
  database = json.load(open(os.path.join(build_path, db_path)))
  files = [make_absolute(entry['file'], entry['directory'])
           for entry in database]

  max_task = args.j
  if max_task == 0:
    max_task = multiprocessing.cpu_count()

  tmpdir = None
  if args.fix or args.export_fixes:
    check_clang_apply_replacements_binary(args)
    tmpdir = tempfile.mkdtemp()

  # Build up a big regexy filter from all command line arguments.
  file_name_re = re.compile('|'.join(args.files))

  return_code = 0
  try:
    # Spin up a bunch of tidy-launching threads.
    task_queue = queue.Queue(max_task)
    # List of files with a non-zero return code.
    failed_files = []
    lock = threading.Lock()
    for _ in range(max_task):
      t = threading.Thread(target=run_tidy,
                           args=(args, tmpdir, build_path, task_queue, lock, failed_files))
      t.daemon = True
      t.start()

    # Fill the queue with files.
    for name in files:
      if file_name_re.search(name):
        task_queue.put(name)

    # Wait for all threads to be done.
    task_queue.join()
    if len(failed_files):
      return_code = 1

  except KeyboardInterrupt:
    # This is a sad hack. Unfortunately subprocess goes
    # bonkers with ctrl-c and we start forking merrily.
    print('\nCtrl-C detected, goodbye.')
    if tmpdir:
      shutil.rmtree(tmpdir)
    os.kill(0, 9)

  if args.export_fixes:
    print('Writing fixes to ' + args.export_fixes + ' ...')
    try:
      merge_replacement_files(tmpdir, args.export_fixes)
    except:
      print('Error exporting fixes.\n', file=sys.stderr)
      traceback.print_exc()
      return_code=1

  if args.fix:
    print('Applying fixes ...')
    try:
      apply_fixes(args, tmpdir)
    except:
      print('Error applying fixes.\n', file=sys.stderr)
      traceback.print_exc()
      return_code=1

  if tmpdir:
    shutil.rmtree(tmpdir)
  sys.exit(return_code)

if __name__ == '__main__':
  main()
