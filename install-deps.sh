#!/bin/sh

# Installs build dependencies. Generally you'll want to sudo-run this.

set -e

# Install plankton
$(cd deps/plankton/src/python && python setup.py install)
