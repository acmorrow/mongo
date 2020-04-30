# Copyright 2020 MongoDB Inc.
#
# Permission is hereby granted, free of charge, to any person obtaining
# a copy of this software and associated documentation files (the
# "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish,
# distribute, sublicense, and/or sell copies of the Software, and to
# permit persons to whom the Software is furnished to do so, subject to
# the following conditions:
#
# The above copyright notice and this permission notice shall be included
# in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY
# KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
# WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
# LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
# OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
# WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#

import os

import SCons

def gatif(env, entry):
    stack=[entry]
    cache=set()
    files=[]
    while stack:
        s = stack.pop()

        if s in cache:
            continue
        cache.add(s)

        files.append(s)
        # scan_for_transitive_install is memoized so it's safe to call it in
        # this loop. If it hasn't already run for a file we need to run it
        # anyway.
        stack.extend(sc for sc in s.children() if sc.has_builder())

    return sorted(files)

def generate_test_execution_aliases(env, test):
    installed = [test]
    if env.get("AUTO_INSTALL_ENABLED", False) and env.GetAutoInstalledFiles(test):
        installed = env.GetAutoInstalledFiles(test)

    target_name = os.path.basename(installed[0].path)
    command = env.Command(
        target="#+{}".format(target_name),
        source=installed[0],
        action="${SOURCES[0]} $UNITTEST_FLAGS",
        NINJA_POOL="console",
    )

    outcome_command = env.Command(
        target=env.File(installed[0]).File('{}.outcome'.format(installed[0])),
        source=installed[0],
        action=SCons.Action.Action(
            "${SOURCES[0]} $UNITTEST_FLAGS > ${TARGET}",
            "RUNNING ${SOURCES[0]}"
        )
    )
    outcome_command_alias = env.Alias("{}.outcome".format(target_name), outcome_command)


    if 'base_test' in target_name or "bson_mutable_test" in target_name:
        #print('AAA CHILDREN', [str(c) for c in installed[0].children()])
        #print('AAA ALL CHILDREN', [str(c) for c in installed[0].all_children()])
        #print('AAA GAIF', [str(c) for c in env.GetAutoInstalledFiles(installed[0])])
        #print('AAA GTIF', [str(c) for c in env.GetTransitivelyInstalledFiles(installed[0])])
        #print('AAA GATIF', [str(c) for c in gatif(env, installed[0])])

        def finalize_install_dependencies_callback(env):
            #print('ZZZ CHILDREN', [str(c) for c in installed[0].children()])
            #print('ZZZ ALL CHILDREN', [str(c) for c in installed[0].all_children()])
            #print('ZZZ GAIF', [str(c) for c in env.GetAutoInstalledFiles(installed[0])])
            #print('ZZZ GTIF', [str(c) for c in env.GetTransitivelyInstalledFiles(installed[0])])
            #print('ZZZ GATIF', [str(c) for c in gatif(env, installed[0])])

            env.Depends(outcome_command, gatif(env, installed[0]))

        env.RegisterFinalizeInstallDependenciesCallback(finalize_install_dependencies_callback)
        env.Alias('test-outcomes', outcome_command_alias)

    env.Alias("test-execution-aliases", command)
    for source in test.sources:
        source_base_name = os.path.basename(source.get_path())
        # Strip suffix
        dot_idx = source_base_name.rfind(".")
        suffix = source_base_name[dot_idx:]
        if suffix in env["TEST_EXECUTION_SUFFIX_BLACKLIST"]:
            continue

        source_name = source_base_name[:dot_idx]
        if target_name == source_name:
            continue

        source_command = env.Command(
            target="#+{}".format(source_name),
            source=installed,
            action="${SOURCES[0]} -fileNameFilter $TEST_SOURCE_FILE_NAME $UNITTEST_FLAGS",
            TEST_SOURCE_FILE_NAME=source_name,
            NINJA_POOL="console",
        )

        env.Alias("test-execution-aliases", source_command)


def exists(env):
    return True


def generate(env):
    # Used for Ninja generator to collect the test execution aliases
    env.Alias("test-execution-aliases")
    env.AddMethod(generate_test_execution_aliases, "GenerateTestExecutionAliases")

    env["TEST_EXECUTION_SUFFIX_BLACKLIST"] = env.get(
        "TEST_EXECUTION_SUFFIX_BLACKLIST", [".in"]
    )

    # TODO: Remove when the new ninja generator is the only supported generator
    env["_NINJA_NO_TEST_EXECUTION"] = True
