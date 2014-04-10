"""Builders for .d files to generate .dtrace.h headers
"""

import os.path

import SCons.Action
import SCons.Node.FS
import SCons.Tool
import SCons.Util

def exists(env):
    return env.Detect(['dtrace'])

def generate(env):

    env["DTRACE"] = env.Detect(['dtrace'])
    env['DTRACESRCSUFFIX'] = '.dtrace'
    env['DTRACEHEADERSUFFIX'] = '_probes.h'
    env["DTRACEFLAGS"] = SCons.Util.CLVar("")
    env["DTRACECOM"] = '$DTRACE $DTRACEFLAGS -o $TARGET -h -s $SOURCE'

    fs = SCons.Node.FS.get_default_fs()

    dtraceh = SCons.Builder.Builder(
        action=SCons.Action.Action('$DTRACECOM', '$DTRACECOMSTR'),
        src_suffix='$DTRACESRCSUFFIX',
        suffix='$DTRACEHEADERSUFFIX',
        target_factory=fs.Entry,
        source_factory=fs.File,
        single_source=True,
    )

    env['BUILDERS']['DTraceH'] = dtraceh
