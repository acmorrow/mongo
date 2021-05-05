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

def _cppForceIncludesGenerator(target, source, env, for_signature):
    forceincludes = env.get('CPPFORCEINCLUDES', [])
    if not forceincludes:
        return []

    if for_signature:
        search_paths = tuple(env.Dir(SCons.PathList.PathList('$CPPPATH').subst_path(env, target, source)))
        forceincludesfiles = [SCons.Node.FS.find_file(f, search_paths) for f in forceincludes]
        target.add_to_implicit(forceincludesfiles)
        return [f.get_csig() for f in forceincludesfiles]

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

def exists(env):
    return True
