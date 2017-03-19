import SCons

import shutil
import os

def _move_and_copy(target = None, source = None, env = None):
    orig = str(target[0])
    backup = orig + ".tmp"
    try:
        os.rename(orig, backup)
    except OSError as e:
        return
    shutil.copy2(backup, orig)
    os.remove(backup)

def _make_builder_softprecious(env, builder):

    orig_target_factory = builder.target_factory

    def new_target_factory(str):
        target = (orig_target_factory or env.Entry)(str)
        target.set_precious()
        return target

    builder.target_factory = new_target_factory

    builder.action = SCons.Action.ListAction([
        SCons.Action.Action(_move_and_copy, None),
        builder.action,
    ])

def generate(env):
    builders = env['BUILDERS']
    for builder in ('Program', 'SharedLibrary', 'LoadableModule'):
        _make_builder_softprecious(env, builders[builder])

def exists(env):
    # By default, the windows linker is incremental. Unless overridden in the environment
    # try it out.
    if env.TargetOSIs('windows') and not "/INCREMENTAL:NO" in env['LINKFLAGS']:
        return True

    # On posix platofrms
    if env.TargetOSIs('posix') and \
       not env.TargetOSIs('darwin') and \
       "-fuse-ld=gold" in env['LINKFLAGS'] and \
       "-Wl,--incremental" in env['LINKFLAGS']:
        return True

    return False
