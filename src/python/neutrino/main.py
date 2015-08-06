#!/usr/bin/python

# Main entry-point for the neutrino parser.


# Set up appropriate path. Bit of a hack but hey.
import sys
import os.path
sys.path.append(os.path.join(os.path.dirname(__file__), '..'))


# Remaining imports.
import analysis
import ast
import data
import optparse
import nparser
import plankton
import re
import schedule
import token


# Encapsulates the compilation of an individual module.
class ModuleCompile(object):

  def __init__(self, manifest_file):
    self.manifest_file = manifest_file
    self.module = None

  def process(self):
    # Read the manifest.
    manifest = self.parse_manifest()
    path = manifest.get_path()
    self.module = ast.Module(path.get_name())
    root = os.path.dirname(self.manifest_file)
    # Scan through and load the source files.
    for file in manifest.get_sources():
      filename = os.path.join(root, file)
      self.parse_source_file(filename)
    # Do post-processing.
    analysis.scope_analyze(self.module)

  def parse_manifest(self):
    source = open(self.manifest_file, "rt").read()
    tokens = token.tokenize(source)
    return nparser.ModuleParser(tokens).parse_module_manifest()

  def parse_source_file(self, name):
    source = open(name, "rt").read()
    tokens = token.tokenize(source)
    nparser.Parser(tokens, self.module, name).parse_program()

  def add_to_library(self, library):
    unbound = self.module.as_unbound_module()
    library.add_module(unbound.path, unbound)


# Encapsulates the compilation of source files into a library.
class LibraryCompile(object):

  def __init__(self, options):
    self.options = options
    self.library = data.Library()

  def run(self):
    self.compile_modules()
    self.write_output()

  def compile_modules(self):
    for module_manifest in self.options["modules"]:
      module = ModuleCompile(module_manifest)
      module.process()
      module.add_to_library(self.library)

  def write_output(self):
    blob = plankton.Encoder().encode(self.library)
    handle = open(self.options['out'], 'wb')
    handle.write(blob)
    handle.close()


# Encapsulates stats relating to the main script.
class Main(object):

  def __init__(self):
    self.options = None
    self.flags = None
    self.scheduler = schedule.TaskScheduler()

  # Parses the script arguments, storing the values in the appropriate fields.
  def parse_arguments(self):
    self.options = plankton.options.parse(sys.argv[1:])
    self.flags = self.options.get_flags()
    compile = self.flags.compile
    if compile:
      self.compile_flags = compile
    else:
      self.compile_flags = None

  # If the filter option is set, filters input and return True. Otherwise
  # returns False.
  def run_filter(self):
    if not self.flags.filter:
      return False
    pattern = re.compile(r'^p64/([a-zA-Z0-9=+/]+)$')
    for line in sys.stdin:
      match = pattern.match(line.strip())
      if match:
        code = match.group(1)
        decoder = plankton.Decoder({})
        if self.flags.disass:
          print decoder.base64disassemble(code)
        else:
          data = decoder.base64decode(code)
          print plankton.stringify(data)
      else:
        print line
    return True

  # Main entry-point.
  def run(self):
    self.parse_arguments()
    if self.run_filter():
      return
    # First load the units to compile without actually doing it.
    self.schedule_files()
    self.schedule_libraries()
    # Then compile everything in the right order.
    self.scheduler.run()

  # Processes any --file arguments. These are used by the nunit tests.
  def schedule_files(self):
    files = self.flags.files or []
    for filename in files:
      source = open(filename, "rt").read()
      tokens = token.tokenize(source)
      module = ast.Module(filename)
      nparser.Parser(tokens, module).parse_program()
      self.schedule_for_compile(module)
      self.schedule_for_output(module)

  def schedule_libraries(self):
    if not self.compile_flags or not "build_library" in self.compile_flags:
      return
    process = LibraryCompile(self.compile_flags["build_library"])
    process.run()

  # Schedules a unit for compilation at the appropriate time relative to any
  # of its dependencies.
  def schedule_for_compile(self, unit):
    # Analysis doesn't depend on anything else so we can just go ahead and get
    # that out of the way.
    analysis.scope_analyze(unit)

  # Schedules the present program of the given unit to be output to stdout when
  # all the prerequisites for doing so have been run.
  def schedule_for_output(self, unit):
    program = unit.get_present_program()
    self.output_value(program)

  def run_parse_input(self, inputs, parse_thunk):
    for expr in inputs:
      tokens = token.tokenize(expr)
      unit = parse_thunk(tokens)
      # Implicitly import the core module into the oldest stage. There needs to
      # better model for this but for now it helps make builtin methods slightly
      # less magic.
      unit.get_oldest_stage().add_import(data.Path(['core']))
      self.schedule_for_compile(unit)
      self.schedule_for_output(unit)

  def output_value(self, value):
    if self.flags.out is None:
      out = sys.stdout
    else:
      out = open(self.flags.out, "wb")
    encoder = plankton.Encoder()
    if self.flags.base64:
      print "p64/%s" % encoder.base64encode(value)
    else:
      out.write(encoder.encode(value))


if __name__ == '__main__':
  Main().run()
