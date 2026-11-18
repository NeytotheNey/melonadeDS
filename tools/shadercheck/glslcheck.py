"""glslcheck.py DIR...: for every dumped compute program (MELONADE_SHADER_DUMP), (1) no compute built-in is
used before the program's layout(local_size_*) in; declaration (Mali-G715 rejects that, Nvidia doesn't),
and (2) glslc (the NDK's, glslang) accepts it as a GLSL ES 3.20 compute shader for OpenGL."""
import glob, os, re, subprocess, sys, tempfile

GLSLC = r'C:\Tools\android-sdk\ndk\28.0.13004108\shader-tools\windows-x86_64\glslc.exe'
BUILTINS = re.compile(r'\bgl_(GlobalInvocationID|LocalInvocationID|LocalInvocationIndex|WorkGroupID|WorkGroupSize|NumWorkGroups)\b')
LAYOUT = re.compile(r'layout\s*\(\s*local_size_x[^)]*\)\s*in\s*;')


def strip_comments(s):
    s = re.sub(r'/\*.*?\*/', lambda m: re.sub(r'[^\n]', ' ', m.group(0)), s, flags=re.S)
    return re.sub(r'//[^\n]*', '', s)


def preprocess(path):
    # glslc's own preprocessor drops what #if leaves out, so the order check sees what the driver does
    out = subprocess.run([GLSLC, '-E', '-fshader-stage=compute', path], capture_output=True, text=True)
    return out.stdout if out.returncode == 0 else None


bad_order = []; bad_glslc = []; n = 0
for d in sys.argv[1:]:
    for path in sorted(glob.glob(os.path.join(d, '*.glsl'))):
        n += 1
        src = preprocess(path)
        if src is None:
            bad_glslc.append((path, 'preprocess failed'))
            src = open(path, encoding='utf-8').read()
        s = strip_comments(src)
        m = LAYOUT.search(s)
        first = BUILTINS.search(s)
        if m is None or (first and first.start() < m.start()):
            line = s[:first.start()].count('\n') + 1 if first else 0
            bad_order.append((os.path.basename(path), first.group(0) if first else 'no layout', line))
        out = subprocess.run([GLSLC, '-fshader-stage=compute', '--target-env=opengl', '-o', os.devnull, path],
                             capture_output=True, text=True)
        if out.returncode != 0:
            bad_glslc.append((os.path.basename(path), (out.stderr or out.stdout).strip().splitlines()[:3]))
print(f'{n} programs')
print(f'order: {len(bad_order)} with a compute built-in before the work group size')
for b in bad_order[:20]:
    print('  ', b)
print(f'glslc: {len(bad_glslc)} rejected')
for b in bad_glslc[:20]:
    print('  ', b)
