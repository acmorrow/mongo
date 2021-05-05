# Copyright 2021 MongoDB Inc.
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

def _add_scanner(builder):

    # We are taking over the target scanner here. If we want to not do
    # that we need to invent a ListScanner concept to inject. What if
    # the other scanner wants a different path_function?
    assert builder.target_scanner is None

    def new_scanner(node, env, path):
        return [env.FindFile(f, path) for f in env.get('CPPFORCEINCLUDES', [])]

    # The 'builder.builder' here is because we need to reach inside
    # the CompositeBuilder that wraps the object builders that come
    # back from createObjBuilders
    builder.builder.target_scanner = SCons.Scanner.Scanner(
        function=new_scanner, path_function=SCons.Script.FindPathDirs('CPPPATH')
    )

def _cppForceIncludesGenerator(target, source, env, for_signature):
    forceincludes = env.get('CPPFORCEINCLUDES', [])
    return env['_concat']('$CPPFORCEINCLUDEPREFIX', forceincludes, '$CPPFORCEINCLUDESUFFIX', env, lambda x: x, target=target, source=source)

def generate(env, **kwargs):
    if not 'CPPFORCEINCLUDEPREFIX' in env:
        if 'msvc' in env.get('TOOLS', []):
            env['CPPFORCEINCLUDEPREFIX'] = '/FI'
        else:
            env['CPPFORCEINCLUDEPREFIX'] = '-include '

    if not 'CPPFORCEINCLUDESUFFIX' in env:
        env['CPPFORCEINCLUDESUFFIX'] = ''

    env['_CPPFORCEINCLUDESGEN'] = _cppForceIncludesGenerator
    env['_CPPFORCEINCLUDESGENLIST'] = ['$_CPPFORCEINCLUDESGEN']

    env.Append(
        _CPPINCFLAGS='$_CPPFORCEINCLUDESGEN'
    )

    for object_builder in SCons.Tool.createObjBuilders(env):
        _add_scanner(object_builder)

def exists(env):
    return True
