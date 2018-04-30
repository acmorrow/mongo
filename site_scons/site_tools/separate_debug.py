# Copyright 2018 MongoDB Inc.
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

import SCons

def _dwo_emitter(target, source, env):
    new_targets = []
    for t in target:

        base, ext = SCons.Util.splitext(str(t))
        if not any(ext == env[osuffix] for osuffix in ['OBJSUFFIX', 'SHOBJSUFFIX']):
            continue

        target_factory = env.get_factory(t.builder.target_factory)

        if 'PDB' in env:
            dwo_targets = env.arg2nodes('$PDB', target_factory, target=t)
        else:
            # TODO: Make $DWOSUFFIX in case GCC ever lets us configure the output name
            dwo_targets = env.arg2nodes('${TARGET.base}.dwo', target_factory, target=t)

        new_targets.extend(dwo_targets)

    return (target + new_targets, source)

def _inject_dwo_emitter(env):
    for object_builder in SCons.Tool.createObjBuilders(env):
        new_emitter = SCons.Builder.DictEmitter()
        for suffix, sub_emitter in object_builder.builder.emitter.items():
            new_emitter[suffix] = SCons.Builder.ListEmitter([
                sub_emitter, _dwo_emitter
            ])
        object_builder.builder.emitter = new_emitter

def _update_builder(env, builder, option, bitcode):

    old_scanner = builder.target_scanner
    old_path_function = old_scanner.path_function

    def new_scanner(node, env, path=()):
        results = old_scanner.function(node, env, path)
        origin = getattr(node.attributes, 'debug_file_for', None)
        if origin:
            origin_results = old_scanner(origin, env, path)
            for origin_result in origin_results:
                origin_result_debug_files = getattr(origin_result.attributes, 'separate_debug_files', None)
                if origin_result_debug_files:
                    results.extend(origin_result_debug_files)
        # TODO: Do we need to do the same sort of drag along for bcsymbolmap files?
        return results

    builder.target_scanner = SCons.Scanner.Scanner(
        function=new_scanner,
        path_function=old_path_function,
    )

    base_action = builder.action
    if not isinstance(base_action, SCons.Action.ListAction):
        base_action = builder.action = SCons.Action.ListAction([base_action])

    # TODO: Make variables for dsymutil and strip, and for the action
    # strings. We should really be running these tools as found by
    # xcrun by default. We should achieve that by upgrading the
    # site_scons/site_tools/xcode.py tool to search for these for
    # us. We could then also remove a lot of the compiler and sysroot
    # setup from the etc/scons/xcode_*.vars files, which would be a
    # win as well.
    if env.TargetOSIs('windows'):
        if option == "objects":
            base_action.list.append(
                SCons.Action.Action(
                    'mspdbcmf /NOLOGO /WX ${PDB}',
                    '${MSPDBCMFCOMSTR}',
                ),
            )

    elif env.TargetOSIs('darwin'):
        if bitcode:
            base_action.list.append(
                SCons.Action.Action(
                    '${DSYMUTIL} -num-threads=1 $TARGET --symbol-map=${TARGET}.bcsymbolmap -o ${TARGET}.dSYM',
                    '${DSYMUTILCOMSTR}',
                ),
            )

        else:
            base_action.list.append(
                SCons.Action.Action(
                    '${DSYMUTIL} -num-threads=1 $TARGET -o ${TARGET}.dSYM',
                    '${DSYMUTILCOMSTR}',
                ),
            )

        base_action.list.append(
            SCons.Action.Action(
                '${STRIP} -Sx ${TARGET}',
                '${STRIPCOMSTR}',
            ),
        )

    elif env.TargetOSIs('posix'):
        base_action.list.extend([
            SCons.Action.Action(
                '${OBJCOPY} --only-keep-debug $TARGET ${TARGET}.debug',
                '${OBJCOPYOKDCOMSTR}'
            ),
            SCons.Action.Action(
                '${OBJCOPY} --strip-debug --add-gnu-debuglink ${TARGET}.debug ${TARGET}',
                '${OBJCOPYSTRIPDEBUGCOMSTR}',
            ),
        ])

        if option == 'objects':
            env['DWPACTION'] = '${DWP} -e ${TARGET}.debug -o ${TARGET}.dwp'
            env['DWPFAKEACTION'] = 'touch ${TARGET}.dwp'
            env['DWPMAYBE'] = SCons.Util.CLVar(['${DWP and DWPACTION or DWPFAKEACTION}'])
            base_action.list.extend([
                SCons.Action.Action(
                    '${DWPMAYBE}',
                    '${DWPCOMSTR}',
                ),
            ])

    builder.action = base_action

    base_emitter = builder.emitter

    def new_emitter(target, source, env):

        bitcode_file = None
        if env.TargetOSIs('windows'):
            debug_files = [env.File(env.subst('hydrated/${PDB}', target=target))]
        elif env.TargetOSIs('darwin'):
            debug_files = [env.Dir(str(target[0]) + '.dSYM')]
            if bitcode:
                bitcode_file = env.File(str(target[0]) + '.bcsymbolmap')
        elif env.TargetOSIs('posix'):
            debug_files = [env.File(str(target[0]) + '.debug')]
            if option == 'objects':
                debug_files.append(env.File(str(target[0]) + '.dwp'))

        for debug_file in debug_files:
            setattr(debug_file.attributes, 'debug_file_for', target[0])
        setattr(target[0].attributes, 'separate_debug_files', debug_files)

        target.extend(debug_files)

        if bitcode_file:
            setattr(bitcode_file.attributes, 'bcsymbolmap_file_for', target[0])
            setattr(target[0].attributes, 'bcsymbolmap_file', bitcode_file)
            target.append(bitcode_file)

        return (target, source)

    new_emitter = SCons.Builder.ListEmitter([base_emitter, new_emitter])
    builder.emitter = new_emitter

def generate(env):
    if not exists(env):
        return

    option = env.GetOption('separate-debug')

    if option == 'auto':
        option = 'objects'

    if option == 'off':
        return

    bitcode = False

    if env.TargetOSIs('windows'):

        if option == 'off':
            env.FatalError('Windows does not support the "off" mode for --separate-debug')

        # When building on visual studio, this sets the name of the debug symbols file
        env['PDB'] = '${TARGET}.pdb'

        # TODO: Ensure mslink in environment tools

        # We override the default SCons PDB generator since the one
        # in mslink unconditionally injects /DEBUG, but we want to control that flag.
        def pdbGenerator(env, target, source, for_signature):
            try:
                return ['/PDB:%s' % target[0].attributes.pdb]
            except (AttributeError, IndexError):
                return None
        env['_PDB'] = pdbGenerator

        if option == 'binaries':
            env['CCPDBFLAGS'] = ['/Z7']
            env.AppendUnique(LINKFLAGS=['/DEBUG'])
        else:
            env['CCPDBFLAGS'] = ['/Zi', '/Fd${PDB}']
            env.AppendUnique(LINKFLAGS=['/DEBUG:fastlink'])
            #env.AppendUnique(LINKFLAGS=['/DEBUG'])
            _inject_dwo_emitter(env)

    elif env.TargetOSIs('darwin'):

        if option != 'objects':
            env.FatalError('The only supported debug info mode on Darwin platforms is "objects"')

        # If we are generating bitcode, add the magic linker flags that
        # hide the bitcode symbols, and override the name of the bitcode
        # symbol map file so that it is determinstically known to SCons
        # rather than being a UUID. We need this so that we can install it
        # under a well known name. We leave it to the evergreen
        # postprocessing to rename to the correct name. I'd like to do
        # this better, but struggled for a long time and decided that
        # later was a better time to address this. We should also consider
        # moving all bitcode setup into a separate tool.
        if any(flag == '-fembed-bitcode' for flag in env['LINKFLAGS']):
            bitcode = True
            env.AppendUnique(LINKFLAGS=[
                '-Wl,-bitcode_hide_symbols',
                '-Wl,-bitcode_symbol_map,${TARGET}.bcsymbolmap',
            ])

    elif env.TargetOSIs('posix'):

        if option == 'objects':
            env.AppendUnique(
                CCFLAGS='-gsplit-dwarf',
                LINKFLAGS='-gsplit-dwarf',
            )
            _inject_dwo_emitter(env)

    else:
        env.FatalError('Don\'t know how to do separate debug in this platform')

    for builder in ['SharedLibrary', 'LoadableModule', 'Program']:
        _update_builder(env, env['BUILDERS'][builder], option, bitcode)

    if not env.get('VERBOSE', True):
        env['DSYMUTILCOMSTR'] = 'Generating debug info for $TARGET into ${TARGET}.dSYM'
        env['STRIPCOMSTR'] = 'Stripping ${TARGET}'
        env['OBJCOPYOKDCOMSTR'] = 'Generating debug info for $TARGET into ${TARGET}.debug'
        env['OBJCOPYSTRIPDEBUGCOMSTR'] = 'Stripping debug info from ${TARGET} and adding .gnu.debuglink to ${TARGET}.debug'
        env['DWPCOMSTR'] = 'Generating .dwp file for $TARGET into ${TARGET}.dwp'
        env['MSPDBCMFCOMSTR'] = 'Rehydrating PDB file ${PDB},'

def exists(env):
    return True
