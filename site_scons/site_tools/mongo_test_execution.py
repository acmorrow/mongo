# Copyright 2019 MongoDB Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import os


def generate_test_execution_aliases(env, test):

    installed = test
    if env.get("AUTO_INSTALL_ENABLED", False) and env.GetAutoInstalledFiles(test):
        installed = env.GetAutoInstalledFiles(test)
    installed = env.Flatten(installed)

    target_name = os.path.basename(installed[0].path)
    command = env.Command(
        target="#+{}".format(target_name),
        source=installed[0],
        action="${SOURCES[0]} $UNITTEST_FLAGS",
        NINJA_POOL="console",
    )

    command2 = env.Command(
        target=env.File(installed[0]).File('{}.outcome'.format(installed[0])),
        source=installed[0],
        action="${SOURCES[0]} $UNITTEST_FLAGS > ${TARGET}",
    )

    command2_alias = env.Alias("{}.outcome".format(target_name), command2)

    if 'base_test' in target_name or "bson_mutable_test" in target_name:
        env.Alias('test-outcomes', command2_alias)

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
