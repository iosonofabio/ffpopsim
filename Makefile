#############################################################################
#
# Licence:	
# Author:	Richard Neher, Fabio Zanini
# Date:		2012/05/15
#
# Description:
# ------------
# Makefile for the PopGenLib library.
#
# The first section deals with platform-specific options and paths.
# In particular, there are two variables that can differ across the audience:
#
# 1. PYTHON_PREFIX: the root of your Python installation. Two common choices
#                   are listed, but you should check your own path. One way
#                   of doing this is to run:
#
#                   which python
#
#                   in a shell, and copy the first chunk of the path.
#
# 2. PYTHON_LD_FLAGS_PLATFORM: the stategy used to build shared libraries.
#                   There are only two options here, depending on whether you
#                   are on an Apple computer or not.
#
# This file tries to guess reasonable values for both variables, by including
# Makefile_guesses. However, if you know their value on your system, please
# set it explicitely below.
#
# There are four main recipes:
# - src: library compilation and (static) linking
# - doc: documentation
# - tests: test cases compilation and linking against the library
# - python: python bindings
#
##==========================================================================
# PLATFORM-DEPENDENT OPTIONS
#
# Please edit the following lines to your needs. If you do not have doxygen
# or python, just leave that line untouched or delete it.
##==========================================================================
CC := gcc
CXX := g++
SWIG := swig
DOXY := doxygen
PYTHON := python2.7

# Try to guess the platform and whether Doxygen and/or Python are installed
include Makefile_guesses

# Apple users would usually use (and possibly edit) the first line
# Linux users the second line
#PYTHON_PREFIX := /Library/Frameworks/EPD64.framework/Versions/Current
#PYTHON_PREFIX := /usr

# Apple users would usually use (and possibly edit) the first line
# Linux users the second line
#PYTHON_LD_FLAGS_PLATFORM := -dynamiclib -flat_namespace -undefined suppress
#PYTHON_LD_FLAGS_PLATFORM := -shared

# PYTHON_PREFIX is actually only used in the following two variables.
# You can also directly edit these if you know what you are doing.
PYTHON_INCLUDES = $(PYTHON_PREFIX)/include/$(PYTHON)
NUMPY_INCLUDES = $(PYTHON_PREFIX)/lib/$(PYTHON)/site-packages/numpy/core/include

############################################################################
# !! DO NOT EDIT BELOW THIS LINE !!
############################################################################
##==========================================================================
# OVERVIEW
##==========================================================================
SRCDIR = src
DOCDIR = doc
TESTSDIR = tests
PYBDIR = $(SRCDIR)/python

.PHONY : all clean src tests doc python clean-src clean-doc clean-tests clean-python
all: src tests $(doc) $(python)
clean: clean-src clean-doc clean-tests clean-python

##==========================================================================
# SOURCE
##==========================================================================
SRC_CXXFLAGS= -O2 -g3 -fPIC

LIBRARY := libPopGenLib.a

HEADER_GENERIC = popgen.h
SOURCE_GENERIC = sample.cpp
OBJECT_GENERIC = $(SOURCE_GENERIC:%.cpp=%.o)

HEADER_LOWD = $(HEADER_GENERIC) popgen_lowd.h
SOURCE_LOWD = hypercube.cpp haploid_lowd.cpp
OBJECT_LOWD = $(SOURCE_LOWD:%.cpp=%.o)

HEADER_HIGHD = $(HEADER_GENERIC) popgen_highd.h
SOURCE_HIGHD = hypercube_function.cpp haploid_highd.cpp
OBJECT_HIGHD = $(SOURCE_HIGHD:%.cpp=%.o)

HEADER_HIV = hivpopulation.h
SOURCE_HIV = $(HEADER_HIV:%.h=%.cpp)
OBJECT_HIV = $(SOURCE_HIV:%.cpp=%.o)

OBJECTS = $(OBJECT_GENERIC) $(OBJECT_LOWD) $(OBJECT_HIGHD) $(OBJECT_HIV)

# Recipes
src: $(SRCDIR)/$(LIBRARY)

$(SRCDIR)/$(LIBRARY): $(OBJECTS:%=$(SRCDIR)/%)
	ar rcs $@ $^

$(OBJECT_GENERIC:%=$(SRCDIR)/%): $(SOURCE_GENERIC:%=$(SRCDIR)/%)
	$(CXX) $(SRC_CXXFLAGS) -c -o $@ $(@:.o=.cpp)

$(OBJECT_LOWD:%=$(SRCDIR)/%): $(SOURCE_LOWD:%=$(SRCDIR)/%) $(HEADER_LOWD:%=$(SRCDIR)/%)
	$(CXX) $(SRC_CXXFLAGS) -c -o $@ $(@:.o=.cpp)

$(OBJECT_HIGHD:%=$(SRCDIR)/%): $(SOURCE_HIGHD:%=$(SRCDIR)/%) $(HEADER_HIGHD:%=$(SRCDIR)/%)
	$(CXX) $(SRC_CXXFLAGS) -c -o $@ $(@:.o=.cpp)

$(OBJECT_HIV:%=$(SRCDIR)/%): $(SOURCE_HIV:%=$(SRCDIR)/%) $(HEADER_HIV:%=$(SRCDIR)/%)
	$(CXX) $(SRC_CXXFLAGS) -c -o $@ $(@:.o=.cpp)

clean-src:
	cd $(SRCDIR); rm -rf $(LIBRARY) *.o *.h.gch

##==========================================================================
# DOCUMENTATION
##==========================================================================
DOXYFILE   = $(DOCDIR)/Doxyfile

# Recipes
doc:
	$(DOXY) $(DOXYFILE)

clean-doc:
	rm -rf $(DOCDIR)/latex $(DOCDIR)/html

##==========================================================================
# TESTS
##==========================================================================
TESTS_CXXFLAGS = -I$(SRCDIR) -Wall -O2 -c -fPIC -g3
TESTS_LDFLAGS = -O2
TEST_LIBDIRS = -L$(CURDIR)/$(SRCDIR)
TESTS_LIBS = -lPopGenLib -lgsl -lgslcblas

TESTS_LOWD = lowd
TESTS_HIGHD = highd

TESTS_SOURCE_LOWD = $(TESTS_LOWD:%=%.cpp)
TESTS_SOURCE_HIGHD = $(TESTS_HIGHD:%=%.cpp)

TESTS_HEADER_LOWD = $(TESTS_LOWD:%=%.h)
TESTS_HEADER_HIGHD = $(TESTS_HIGHD:%=%.h)

TESTS_OBJECT_LOWD = $(TESTS_LOWD:%=%.o)
TESTS_OBJECT_HIGHD = $(TESTS_HIGHD:%=%.o)

# Recipes
tests: $(SRCDIR)/$(LIBRARY) $(TESTS_LOWD:%=$(TESTSDIR)/%) $(TESTS_HIGHD:%=$(TESTSDIR)/%)

$(TESTS_LOWD:%=$(TESTSDIR)/%): $(TESTS_OBJECT_LOWD:%=$(TESTSDIR)/%) $(SRCDIR)/$(LIBRARY)
	$(CXX) $(TESTS_LDFLAGS) $^ $(TEST_LIBDIRS) $(TESTS_LIBS) -o $@

$(TESTS_OBJECT_LOWD:%=$(TESTSDIR)/%): $(TESTS_SOURCE_LOWD:%=$(TESTSDIR)/%) $(TESTS_HEADER_LOWD:%=$(TESTSDIR)/%)
	$(CXX) $(TESTS_CXXFLAGS) -c $(@:.o=.cpp) -o $@

$(TESTS_HIGHD:%=$(TESTSDIR)/%): $(TESTS_OBJECT_HIGHD:%=$(TESTSDIR)/%) $(OBJECT_HIV:%=$(SRCDIR)/%) $(SRCDIR)/$(LIBRARY)
	$(CXX) $(TESTS_LDFLAGS) $^ $(TEST_LIBDIRS) $(TESTS_LIBS) -o $@

$(TESTS_OBJECT_HIGHD:%=$(TESTSDIR)/%): $(TESTS_SOURCE_HIGHD:%=$(TESTSDIR)/%) $(TESTS_HEADER_HIGHD:%=$(TESTSDIR)/%)
	$(CXX) $(TESTS_CXXFLAGS) -c $(@:.o=.cpp) -o $@

clean-tests:
	cd $(TESTSDIR); rm -rf *.o $(TESTS_LOWD) $(TESTS_HIGHD)

##==========================================================================
# PYTHON BINDINGS
##==========================================================================
SWIGFLAGS = -c++ -python -O -castmode -keyword

SWIG_INTERFACE = PopGenLib.i
SWIG_WRAP = $(SWIG_INTERFACE:%.i=%_wrap.cpp)
SWIG_WRAP_OBJECT = $(SWIG_WRAP:%.cpp=%.o)
SWIG_OBJECT = $(SWIG_INTERFACE:%.i=_%.so)
SWIG_PYMODULE = $(SWIG_INTERFACE:%.i=%.py)
SWIG_PYCMODULE = $(SWIG_INTERFACE:%.i=%.pyc)
SWIG_SUPPORT_1 = popgen_highd.i
SWIG_SUPPORT_2 = hivpopulation.i
SWIG_SUPPORT_3 = popgen_lowd.i
SWIG_SUPPORT_4 = popgen.i

PYTHON_CFLAGS = -O2 -g3 -fPIC -I$(SRCDIR) -I$(PYTHON_INCLUDES) -I$(NUMPY_INCLUDES)
PYTHON_LDFLAGS= -O2 -g3 -fPIC $(PYTHON_LD_FLAGS_PLATFORM)
PYTHON_LIBDIRS = -L$(CURDIR)/$(SRCDIR)
PYTHON_LIBS = -lPopGenLib -lgsl -lgslcblas

# Recipes
python: $(SWIG_OBJECT:%=$(PYBDIR)/%)

$(SWIG_PYMODULE:%=$(PYBDIR)/%) $(SWIG_PYMCODULE:%=$(PYBDIR)/%) $(SWIG_OBJECT:%=$(PYBDIR)/%): $(SWIG_WRAP:%=$(PYBDIR)/%) $(SWIG_SOURCE:%=$(PYBDIR)/%) $(DISTUTILS_SETUP:%=$(PYBDIR)/%) $(SRCDIR)/$(LIBRARY)
	$(CC) $(PYTHON_CFLAGS) -c $(SWIG_WRAP:%=$(PYBDIR)/%) -o $(SWIG_WRAP_OBJECT:%=$(PYBDIR)/%)
	$(CXX) $(PYTHON_LDFLAGS) $(SWIG_WRAP_OBJECT:%=$(PYBDIR)/%) $(PYTHON_LIBDIRS) $(PYTHON_LIBS) -o $(SWIG_OBJECT:%=$(PYBDIR)/%)
	cp $(SWIG_PYMODULE:%=$(PYBDIR)/%) $(SWIG_PYMODULE:%=$(TESTSDIR)/%)
	cp $(SWIG_OBJECT:%=$(PYBDIR)/%) $(SWIG_OBJECT:%=$(TESTSDIR)/%)

$(SWIG_WRAP:%=$(PYBDIR)/%): $(SWIG_HEADER_HIV:%=$(PYBDIR)/%) $(SWIG_INTERFACE:%=$(PYBDIR)/%) $(SRCDIR)/$(LIBRARY) $(SWIG_SUPPORT_1:%=$(PYBDIR)/%) $(SWIG_SUPPORT_2:%=$(PYBDIR)/%) $(SWIG_SUPPORT_3:%=$(PYBDIR)/%) $(SWIG_SUPPORT_4:%=$(PYBDIR)/%)
	$(SWIG) $(SWIGFLAGS) -o $@ $(SWIG_INTERFACE:%=$(PYBDIR)/%)

clean-python:
	cd $(PYBDIR); rm -rf $(SWIG_WRAP) $(SWIG_OBJECT) $(SWIG_PYMODULE) $(SWIG_PYCMODULE)
	cd $(TESTSDIR); rm -rf $(SWIG_OBJECT) $(SWIG_PYMODULE) $(SWIG_PYCMODULE)

#############################################################################
