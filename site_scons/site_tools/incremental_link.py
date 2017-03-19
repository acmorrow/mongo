import SCons

def _tag_as_precious(target, source, env):
    env.Precious(target)
    return target, source

def generate(env):
    builders = env['BUILDERS']
    for builder in ('Program', 'SharedLibrary', 'LoadableModule'):
        emitter = builders[builder].emitter
        builders[builder].emitter = SCons.Builder.ListEmitter([
            emitter,
            _tag_as_precious,
        ])

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
