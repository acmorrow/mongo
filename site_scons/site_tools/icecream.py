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

import SCons

import os
import re
import subprocess
import urllib

from pkg_resources import parse_version

_icecream_version_min = parse_version("1.1rc2")
_icecream_version_gcc_remote_cpp = parse_version("1.2")


# I'd prefer to use value here, but amazingly, its __str__ returns the
# *initial* value of the Value and not the built value, if
# available. That seems like a bug. In the meantime, make our own very
# sinmple Substition thing.
class _BoundSubstitution:
    def __init__(self, env, expression):
        self.env = env
        self.expression = expression
        self.result = None

    def __str__(self):
        if self.result is None:
            self.result = self.env.subst(self.expression)
        return self.result


def icecc_create_env(env, target, source, for_signature):
    # Safe to assume unix here because icecream only works on Unix
    mkdir = "mkdir -p ${TARGET.dir}"

    # Create the env, use awk to get just the tarball name and we store it in
    # the shell variable $ICECC_VERSION_TMP so the subsequent mv command and
    # store it in a known location. Add any files requested from the user environment.
    create_env = "ICECC_VERSION_TMP=$$(${SOURCES[0]} --$ICECC_COMPILER_TYPE ${SOURCES[1]} ${SOURCES[2]}"
    for addfile in env.get('ICECC_CREATE_ENV_ADDFILES', []):
        if (type(addfile) == tuple
            and len(addfile) == 2):
            if env['ICECREAM_VERSION'] > parse_version('1.1'):
                raise Exception("This version of icecream does not support addfile remapping.")
            create_env += " --addfile {}={}".format(
                env.File(addfile[0]).srcnode().abspath,
                env.File(addfile[1]).srcnode().abspath)
            env.Depends(target, addfile[1])
        elif type(addfile) == str:
            create_env += " --addfile {}".format(env.File(addfile).srcnode().abspath)
            env.Depends(target, addfile)
        else:
            # NOTE: abspath is required by icecream because of
            # this line in icecc-create-env:
            # https://github.com/icecc/icecream/blob/10b9468f5bd30a0fdb058901e91e7a29f1bfbd42/client/icecc-create-env.in#L534
            # which cuts out the two files based off the equals sign and
            # starting slash of the second file
            raise Exception("Found incorrect icecream addfile format: {}" +
                "\nicecream addfiles must be a single path or tuple path format: " +
                "('chroot dest path', 'source file path')".format(
                str(addfile)))
    create_env += " | awk '/^creating .*\\.tar\\.gz/ { print $$2 }')"

    # Simply move our tarball to the expected locale.
    mv = "mv $$ICECC_VERSION_TMP $TARGET"

    # Daisy chain the commands and then let SCons Subst in the rest.
    cmdline = f"{mkdir} && {create_env} && {mv}"
    return cmdline


def generate(env):

    if not exists(env):
        return

    # icecc lower then 1.1 supports addfile remapping accidentally
    # and above it adds an empty cpuinfo so handle cpuinfo issues for icecream
    # below version 1.1
    if (env['ICECREAM_VERSION'] <= parse_version('1.1')
        and env.ToolchainIs("clang")
        and os.path.exists('/proc/cpuinfo')):
        env.AppendUnique(ICECC_CREATE_ENV_ADDFILES=[('/proc/cpuinfo', '/dev/null')])

    # If we are going to load the ccache tool, but we haven't done so
    # yet, then explicitly do it now. We need the ccache tool to be in
    # place before we setup icecream because we need to do things a
    # little differently if ccache is in play. If you don't use the
    # TOOLS variable to configure your tools, you should explicitly
    # load the ccache tool before you load icecream.
    ccache_enabled = "CCACHE_VERSION" in env
    if "ccache" in env["TOOLS"] and not ccache_enabled:
        env.Tool("ccache")

    # Absoluteify, so we can derive ICERUN
    env["ICECC"] = env.WhereIs("$ICECC")

    if not "ICERUN" in env:
        env["ICERUN"] = env.File("$ICECC").File("icerun")

    # Absoluteify, for parity with ICECC
    env["ICERUN"] = env.WhereIs("$ICERUN")

    if not "ICECC_CREATE_ENV" in env:
        env["ICECC_CREATE_ENV"] = env.File("$ICECC").File("icecc-create-env")

    # Absoluteify, for parity with ICECC
    env["ICECC_CREATE_ENV"] = env.WhereIs("$ICECC_CREATE_ENV")

    # Make CC and CXX absolute paths too. It is better for icecc.
    env["CC"] = env.WhereIs("$CC")
    env["CXX"] = env.WhereIs("$CXX")

    if not 'ICECREAM_TARGET_DIR' in env:
        # TODO: This is MongoDB specific
        env['ICECREAM_TARGET_DIR'] = env.Dir('$BUILD_ROOT/scons/icecream')

    if 'ICECC_VERSION' in env and bool(env['ICECC_VERSION']):

        # TODO: Handle URLS

        if env["ICECC_VERSION"].startswith("http"):

            quoted = urllib.parse.quote(env['ICECC_VERSION'], safe=[])

            # Use curl / wget to download the toolchain because SCons (and ninja)
            # are better at running shell commands than Python functions.
            #
            # TODO: This all happens SCons side now. Should we just use python to
            # fetch instead?
            curl = env.WhereIs("curl")
            wget = env.WhereIs("wget")

            if curl:
                cmdstr = "curl -L"
            elif wget:
                cmdstr = "wget"
            else:
                raise Exception(
                    "You have specified an ICECC_VERSION that is a URL but you have neither wget nor curl installed."
                )

            # Make a clone so that the assignment to ICECC_VERSION doesn't perturb the
            # download URL.
            downloadEnv = env.Clone()
            env['ICECC_VERSION'] = icecc_version_file = downloadEnv.Command(
                target=f"$ICECREAM_TARGET_DIR/{quoted}",
                source=[env.Value(quoted)],
                action=[
                    f"{cmdstr} -o $TARGET $ICECC_VERSION",
                ],
                NINJA_SKIP=True,
            )[0]

        else:
            # Convert the users selection into a File node and do some basic validation
            env['ICECC_VERSION'] = icecc_version_file = env.File('$ICECC_VERSION')

            if not icecc_version_file.exists():
                raise Exception(
                    'The ICECC_VERSION variable set set to {}, but this file does not exist'.format(icecc_version_file)
                )

        # TODO: Tar.gz suffix check

        # This is what we are going to call the file names as known to SCons on disk
        env["ICECC_VERSION_ID"] = "user_provided." + icecc_version_file.name;

    else:

        env["ICECC_COMPILER_TYPE"] = env.get(
            "ICECC_COMPILER_TYPE", os.path.basename(env.WhereIs("${CC}"))
        )

        # This is what we are going to call the file names as known to SCons on disk. We do the
        # subst early so that we can call `replace` on the result.
        env["ICECC_VERSION_ID"] = env.subst("icecc-create-env.${CC}${CXX}.tar.gz").replace("/", "_")

        # Make a new environment for this builder so we don't add it to the global environment,
        buildEnv = env.Clone()
        buildEnv["ICECCENVCOMSTR"] = buildEnv.get("ICECCENVCOMSTR", "Generating environment: $TARGET")
        buildEnv.Append(
            BUILDERS={
                "IcecreamEnv": SCons.Builder.Builder(
                    action=SCons.Action.CommandGeneratorAction(
                        icecc_create_env, {"comstr": "$ICECCENVCOMSTR"},
                    )
                )
            }
        )

        env["ICECC_VERSION"] = icecc_version_file = buildEnv.IcecreamEnv(
            target="$ICECREAM_TARGET_DIR/$ICECC_VERSION_ID",
            source=[
                "$ICECC_CREATE_ENV",
                "$CC",
                "$CXX"
            ],
            NINJA_SKIP=True
        )[0]

    # At this point, all paths above have produced a file of some sort. We now move on
    # to producing our own signature for this local file.

    targetbase_kwargs = {
        'TARGETBASE_DIR' : '$ICECREAM_TARGET_DIR',
        'TARGETBASE_FILE' : '$ICECC_VERSION_ID',
        'TARGETBASE' : '$TARGETBASE_DIR/$TARGETBASE_FILE',
    }


    # The name of the file the user gave us is not to be trusted. Nor
    # is the name generated by icecc-create-env. We hardlink the file
    # that we are working with into our build directory under a stable
    # name that we trust. If we can't hardlink we make a copy, so that
    # we can hardlink again below without issue.
    icecc_version_copy_or_link = env.File(env.Command(
        target=[
            '${TARGETBASE}.local',
        ],
        source=icecc_version_file,
        action=[
            "ln -f ${SOURCES[0]} ${TARGETS[0]} || cp -f ${SOURCES[0]} ${TARGETS[0]}",
        ],

        NINJA_SKIP=True,
        **targetbase_kwargs,
    ))

    # There is no point caching this. If it is a hardlink, it is cheap
    # to re-create. If it is a copy, it might be large and we don't
    # need to put it in the cache.
    env.NoCache(icecc_version_copy_or_link)

    # Now, we compute our own signature of the local link (or copy) of
    # the compiler package, and create yet another link to the
    # compiler package with a name containing our computed
    # signature. Now we know that we can give this filename to icecc
    # and it will be assured to really reflect the contents of the
    # package, and not the arbitrary naming of the file as found on
    # the users filesystem or from icecc-create-env. We put the
    # absolute path to that filename into a file that we can read
    # from.
    icecc_version_info = env.File(env.Command(
        target=[
            '${TARGETBASE}.sha256',
            '${TARGETBASE}.sha256.path',
        ],
        source=icecc_version_copy_or_link,
        action=[
            "shasum -b -a 256 ${SOURCES[0]} | awk '{ print $1 }' > ${TARGETS[0]}",
            "ln -f ${SOURCES[0]} ${TARGETS[0].dir}/icecream_py_csig_$$(cat ${TARGETS[0]}).tar.gz",
            "echo ${TARGETS[0].dir.abspath}/icecream_py_csig_$$(cat ${TARGETS[0]}).tar.gz > ${TARGETS[1]}",
        ],

        NINJA_SKIP=True,
        **targetbase_kwargs,
    ))

    # Create a value node that, when built, contains the result of
    # reading the contents of the hashpath file. This way we can pull
    # the value out of the file and substitute it into our wrpaper
    # script.
    valueBuilderEnv = env.Clone()
    def update_value(env, target, source):
        target[0].write(source[0].get_text_contents())

    valueBuilderEnv['BUILDERS']['UpdateValue'] = SCons.Builder.Builder(action=update_value)
    icecc_version_string_value = valueBuilderEnv.UpdateValue(
        target=valueBuilderEnv.Value(None),
        source=[icecc_version_info[1]],
        NINJA_SKIP=True,
    )[0]

    def icecc_version_string_generator(source, target, env, for_signature):
        if for_signature:
            return icecc_version_string_value.get_csig()
        return icecc_version_string_value.read()

    # Set the values that will be interpolated into the run-icecc script.
    env['ICECC_VERSION'] = icecc_version_string_generator

    # If necessary, we include the users desired architecture in the
    # interpolated file.
    icecc_version_arch_string = str()
    if "ICECC_VERSION_ARCH" in env:
        icecc_version_arch_string = "${ICECC_VERSION_ARCH}:"

    # Finally, create the run-icecc wrapper script. The contents will
    # re-invoke icecc with our sha256 sum named file, ensuring that we
    # trust the signature to be appropriate. In a pure SCons build, we
    # actually wouldn't need this Substfile, we could just set
    # env['ENV]['ICECC_VERSION'] to the Value node above. But that
    # won't work for Ninja builds where we can't ask for the contents
    # of such a node easily. Creating a Substfile means that SCons
    # will take care of generating a file that Ninja can use.
    run_icecc = env.Substfile(
        target="$ICECREAM_TARGET_DIR/run-icecc",
        source=[
            # TODO: put this in the tree with the tool somehow?
            "etc/run-icecc.in",
        ],
        SUBST_DICT={
            '@icecc@' : '$ICECC',
            '@icecc_version@' : '$ICECC_VERSION',
            '@icecc_version_arch@' : icecc_version_arch_string,
        },
    )

    env.AddPostAction(
        run_icecc,
        action=SCons.Defaults.Chmod('$TARGET', "u+x"),
    )

    env.Depends(
        target=run_icecc,
        dependency=[

            # TODO: Without the ICECC dependency, changing ICECC doesn't cause the Substfile
            # to regenerate. Why is this?
            '$ICECC',

            icecc_version_string_value,
        ],
    )

    env['ICECREAM_RUN_ICECC'] = run_icecc[0]

    def icecc_toolchain_dependency_emitter(target, source, env):
        if "conftest" not in str(target[0]):
            env.Depends(target, "$ICECREAM_RUN_ICECC")
        return target, source

    # Cribbed from Tool/cc.py and Tool/c++.py. It would be better if
    # we could obtain this from SCons.
    _CSuffixes = [".c"]
    if not SCons.Util.case_sensitive_suffixes(".c", ".C"):
        _CSuffixes.append(".C")

    _CXXSuffixes = [".cpp", ".cc", ".cxx", ".c++", ".C++"]
    if SCons.Util.case_sensitive_suffixes(".c", ".C"):
        _CXXSuffixes.append(".C")

    suffixes = _CSuffixes + _CXXSuffixes
    for object_builder in SCons.Tool.createObjBuilders(env):
        emitterdict = object_builder.builder.emitter
        for suffix in emitterdict.keys():
            if not suffix in suffixes:
                continue
            base = emitterdict[suffix]
            emitterdict[suffix] = SCons.Builder.ListEmitter(
                [base, icecc_toolchain_dependency_emitter]
            )

    if env.ToolchainIs("clang"):
        env["ENV"]["ICECC_CLANG_REMOTE_CPP"] = 1
    elif env.ToolchainIs("gcc"):
        if env["ICECREAM_VERSION"] < _icecream_version_gcc_remote_cpp:
            # We aren't going to use ICECC_REMOTE_CPP because icecc
            # 1.1 doesn't offer it. We disallow fallback to local
            # builds because the fallback is serial execution.
            env["ENV"]["ICECC_CARET_WORKAROUND"] = 0
        else:
            if ccache_enabled:
                # Newer versions of Icecream will drop -fdirectives-only from
                # preprocessor and compiler flags if it does not find a remote
                # build host to build on. ccache, on the other hand, will not
                # pass the flag to the compiler if CCACHE_NOCPP2=1, but it will
                # pass it to the preprocessor. The combination of setting
                # CCACHE_NOCPP2=1 and passing the flag can lead to build
                # failures.

                # See: https://jira.mongodb.org/browse/SERVER-48443

                # We have an open issue with Icecream and ccache to resolve the
                # cause of these build failures. Once the bug is resolved and
                # the fix is deployed, we can remove this entire conditional
                # branch and make it like the one for clang.
                # TODO: https://github.com/icecc/icecream/issues/550
                env["ENV"].pop("CCACHE_NOCPP2", None)
                env["ENV"]["CCACHE_CPP2"] = 1
                try:
                    env["CCFLAGS"].remove("-fdirectives-only")
                except ValueError:
                    pass
            else:
                # If we can, we should make Icecream do its own preprocessing
                # to reduce concurrency on the local host. We should not do
                # this when ccache is in use because ccache will execute
                # Icecream to do its own preprocessing and then execute
                # Icecream as the compiler on the preprocessed source.
                env["ENV"]["ICECC_REMOTE_CPP"] = 1

    if "ICECC_SCHEDULER" in env:
        env["ENV"]["USE_SCHEDULER"] = env["ICECC_SCHEDULER"]

    # If ccache is in play we actually want the icecc binary in the
    # CCACHE_PREFIX environment variable, not on the command line, per
    # the ccache documentation on compiler wrappers. Otherwise, just
    # put $ICECC on the command line. We wrap it in the magic "don't
    # consider this part of the build signature" sigils in the hope
    # that enabling and disabling icecream won't cause rebuilds. This
    # is unlikely to really work, since above we have maybe changed
    # compiler flags (things like -fdirectives-only), but we still try
    # to do the right thing.
    if ccache_enabled:
        # TODO: Why does ccache need the prefix to be absolute?
        env["ENV"]["CCACHE_PREFIX"] = _BoundSubstitution(env, "${ICECREAM_RUN_ICECC.abspath}")
    else:
        # Make a generator to expand to ICECC in the case where we are
        # not a conftest. We never want to run conftests
        # remotely. Ideally, we would do this for the CCACHE_PREFIX
        # case above, but unfortunately if we did we would never
        # actually see the conftests, because the BoundSubst means
        # that we will never have a meaningful `target` variable when
        # we are in ENV. Instead, rely on the ccache.py tool to do
        # it's own filtering out of conftests.
        def icecc_generator(target, source, env, for_signature):
            if "conftest" not in str(target[0]):
                return '$ICECREAM_RUN_ICECC'
            return ''
        env['ICECC_GENERATOR'] = icecc_generator

        icecc_string = "$( $ICECC_GENERATOR $)"
        env["CCCOM"] = " ".join([icecc_string, env["CCCOM"]])
        env["CXXCOM"] = " ".join([icecc_string, env["CXXCOM"]])
        env["SHCCCOM"] = " ".join([icecc_string, env["SHCCCOM"]])
        env["SHCXXCOM"] = " ".join([icecc_string, env["SHCXXCOM"]])

    # Make common non-compile jobs flow through icerun so we don't
    # kill the local machine. It would be nice to plumb ICERUN in via
    # SPAWN or SHELL but it is too much. You end up running `icerun
    # icecc ...`, and icecream doesn't handle that. We could try to
    # filter and only apply icerun if icecc wasn't present but that
    # seems fragile. If you find your local machine being overrun by
    # jobs, figure out what sort they are and extend this part of the
    # setup.
    icerun_commands = [
        "ARCOM",
        "LINKCOM",
        "PYTHON",
        "SHLINKCOM",
    ]

    for command in icerun_commands:
        if command in env:
            env[command] = " ".join(["$( $ICERUN $)", env[command]])

    # Uncomment these to debug your icecc integration
    # env['ENV']['ICECC_DEBUG'] = 'debug'
    # env['ENV']['ICECC_LOGFILE'] = 'icecc.log'


def exists(env):
    # Assume the tool has run if we already know the version.
    if "ICECREAM_VERSION" in env:
        return True

    icecc = env.get("ICECC", False)
    if not icecc:
        return False
    icecc = env.WhereIs(icecc)
    if not icecc:
        return False

    pipe = SCons.Action._subproc(
        env,
        SCons.Util.CLVar(icecc) + ["--version"],
        stdin="devnull",
        stderr="devnull",
        stdout=subprocess.PIPE,
    )

    if pipe.wait() != 0:
        return False

    validated = False
    for line in pipe.stdout:
        line = line.decode("utf-8")
        if validated:
            continue  # consume all data
        version_banner = re.search(r"^ICECC ", line)
        if not version_banner:
            continue
        icecc_version = re.split("ICECC (.+)", line)
        if len(icecc_version) < 2:
            continue
        icecc_version = parse_version(icecc_version[1])
        if icecc_version >= _icecream_version_min:
            validated = True

    if validated:
        env['ICECREAM_VERSION'] = icecc_version

    return validated
