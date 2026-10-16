"""Build a title-scoped DCLD00001 installer using the house's existing PS4 helper.

The canonical helper/Scene-Collective SDK remain on the NAS, untouched. Generated
target-specific source and credentials stay in a fresh private build directory.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--house-project", type=Path, required=True)
    p.add_argument("--connection", type=Path, required=True)
    p.add_argument("--preserved-runtime", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--package", required=True)
    p.add_argument("--service-port", type=int, required=True)
    p.add_argument("--previous-launch-receipt", type=Path,
                   help="Resume only this title's previously recorded native app ID for scoped closing")
    a = p.parse_args()
    if not re.fullmatch(r"/user/data/pkg/DCLD00001-[A-Za-z0-9_.-]+\.pkg", a.package):
        p.error("Only a dedicated DCLD00001 package path is accepted")
    if not 1024 <= a.service_port <= 65535:
        p.error("Invalid dedicated service port")
    house = a.house_project.resolve(strict=True)
    runtime = a.preserved_runtime.resolve(strict=True)
    preserved = runtime / "control-build-darkcloud"
    compiler = runtime / "tools/llvm/bin"
    output = a.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    cfg = json.loads(a.connection.read_text(encoding="utf-8-sig"))
    previous_app = -1
    previous_launch_hash = None
    if a.previous_launch_receipt:
        previous_bytes = a.previous_launch_receipt.read_bytes()
        previous = json.loads(previous_bytes)
        previous_app = int(previous['result']['launch_result'])
        if (previous.get('operation') != 'launch' or previous.get('title') != 'DCLD00001'
                or previous.get('host') != cfg['host']
                or not previous.get('package', '').startswith('/user/data/pkg/DCLD00001-')
                or previous['result'].get('phase') != 'launched'
                or previous_app & 0xFF000000 != 0x60000000
                or int(previous['result']['last_result'], 16) != previous_app):
            raise RuntimeError('Previous launch receipt does not prove this console/title application ID')
        previous_launch_hash = hashlib.sha256(previous_bytes).hexdigest()
    for header in preserved.glob("*.h"):
        if header.name != "credential.h":
            shutil.copyfile(header, output / header.name)
    (output / "credential.h").write_text(
        '#define CONTROL_TOKEN "' + cfg["token"] + '"\n#define CONTROL_USER_ID ' + str(cfg["user_id"]) + '\n')
    original = (house / "control/service.c").read_text()
    content = original.replace('#define TITLE "MKRT00001"', '#define TITLE "DCLD00001"')
    content = content.replace('#define PACKAGE "/user/data/pkg/SpaghettiKart-PS4-01.00.pkg"',
                              '#define PACKAGE "' + a.package + '"')
    content = content.replace('"SpaghettiKart PS4"', '"Chronicle PS4 Mods"')
    content = content.replace('/data/cole-ps4-control.log', '/data/darkcloud/mods-installer.log')
    content = content.replace('htons(8764)', f'htons({a.service_port})')
    content = content.replace('"HTTP listening",8764', f'"HTTP listening",{a.service_port}')
    # The historical helper's failed-task cleanup is intentionally not exposed
    # by this title-scoped service. It referred to a different title's task.
    content, count = re.subn(r'static int clear_failed_install\(void\)\{.*?\n\}\n(?=static int launch)',
                             '', content, flags=re.S)
    if count != 1:
        raise RuntimeError("Canonical helper cleanup boundary changed; inspect before building")
    content = '\n'.join(line for line in content.splitlines() if 'POST /clear_failed_install' not in line) + '\n'
    if 'MKRT00001' in content or 'SpaghettiKart' in content or 'clear_failed_install' in content:
        raise RuntimeError("Unexpected unrelated title/operation remains")
    # Native app close ABI: LightningMods/PS4-daemon-writeup/writeup.md.
    # Only the app ID returned by our own fixed-title launch (or a verified
    # prior receipt) is accepted, and it must still be the current big app.
    content = content.replace('installed_app=-1, running=1',
                              f'installed_app={previous_app}, running=1')
    content = content.replace('static int (*big_app)(void);',
                              'static int (*big_app)(void);\nstatic int (*kill_app)(unsigned int,int,int,int);')
    content = content.replace('static int launch(void) {', '''static int stop_game(void) {
 if(!kill_app||!big_app)return -2;
 if(installed_app<0||((unsigned)installed_app&0xff000000U)!=0x60000000U)return -8;
 const int active=big_app();log_line("verify owned running application",active);
 if(active!=installed_app)return -9;
 phase="stopping_owned_game";
 const int r=kill_app((unsigned)installed_app,-1,0,0);log_line(phase,r);
 if(r==0){installed_app=-1;phase="owned_game_stop_requested";}return r;
}
static int launch(void) {''')
    content = content.replace('if(user_mod>=0){SYMBOL(user_mod,user_init,',
        'if(sys_mod>=0)SYMBOL(sys_mod,kill_app,"sceSystemServiceKillApp");\n if(user_mod>=0){SYMBOL(user_mod,user_init,')
    content = content.replace('else if(REQUEST("POST /shutdown HTTP/1.1\\r\\n"))',
        'else if(REQUEST("POST /stop HTTP/1.1\\r\\n")){last_result=stop_game();status(out,sizeof(out));reply(client,200,out);}\n else if(REQUEST("POST /shutdown HTTP/1.1\\r\\n"))')
    if content.count('static int stop_game(void)') != 1 or 'POST /stop HTTP' not in content:
        raise RuntimeError('Canonical helper close boundaries changed; inspect before building')
    # The console's ShellUI trace reports Invalid User Id (-1) independently of
    # the later GPU hang. Observe the actual launch identity without changing it.
    launch_user = 'int user=-1;int r=foreground_user(&user);if(r||user<0)user=CONTROL_USER_ID;'
    if content.count(launch_user) != 1:
        raise RuntimeError('Canonical helper launch-user boundary changed; inspect before building')
    content = content.replace(launch_user, '''int user=-1;int r=foreground_user(&user);
 log_line("launch foreground-user query status",r);log_line("launch queried user",user);
 if(r||user<0)user=CONTROL_USER_ID;
 log_line("launch selected user",user);''')
    target = output / "service.c"
    target.write_text(content)
    flags = ['-target', 'x86_64-pc-freebsd12', '-Os', '-std=c11', '-ffunction-sections',
             '-fdata-sections', '-ffreestanding', '-fno-builtin', '-fno-stack-protector',
             '-fno-asynchronous-unwind-tables', '-mno-red-zone', '-masm=intel',
             '-march=btver2', '-mcmodel=small', '-fPIC', '-I' + str(output),
             '-I' + str(house / 'flat-sdk-source/libPS4/include'),
             '-Wno-incompatible-pointer-types', '-Wno-pointer-sign']
    commands = []

    def run(command):
        command = [str(x) for x in command]
        commands.append(command)
        result = subprocess.run(command, capture_output=True, text=True,
                                creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        with (output / 'build.log').open('a') as log:
            log.write(result.stdout + result.stderr)
        result.check_returncode()
        return result.stdout

    run([compiler / 'clang.exe', *flags, '-c', target, '-o', output / 'service.o'])
    run([compiler / 'ld.lld.exe', '-T', house / 'control/linker.x', '--gc-sections',
         '--build-id=none', '-o', output / 'service.elf', preserved / 'start.o',
         output / 'service.o', preserved / 'memory-shim.o', preserved / 'libPS4.a'])
    readelf = runtime / 'tools/llvm-mingw-20251021-ucrt-x86_64/bin/llvm-readelf.exe'
    elf = run([readelf, '-h', '-r', '-l', output / 'service.elf'])
    (output / 'elf.txt').write_text(elf)
    if 'Entry point address:               0x0' not in elf or 'There are no relocations' not in elf:
        raise RuntimeError('Flat payload entry/relocation validation failed')
    payload = output / 'chronicle-install-helper.bin'
    run([compiler / 'llvm-objcopy.exe', '-O', 'binary', output / 'service.elf', payload])
    evidence = {'title': 'DCLD00001', 'package': a.package, 'service_port': a.service_port,
                'bytes': payload.stat().st_size, 'sha256': hashlib.sha256(payload.read_bytes()).hexdigest(),
                'canonical_helper_sha256': hashlib.sha256(original.encode()).hexdigest(),
                'status': 'compiled; injection and installation not yet attempted', 'commands': commands,
                'attribution': 'House control service; Scene-Collective libPS4 GPL-3.0; BGFT/OpenOrbis interfaces'}
    evidence.update(previous_app_id=previous_app, previous_launch_receipt_sha256=previous_launch_hash,
                    close_api_reference='https://github.com/LightningMods/PS4-daemon-writeup/blob/main/writeup.md')
    (output / 'build-evidence.json').write_text(json.dumps(evidence, indent=2) + '\n')
    print(json.dumps({k: v for k, v in evidence.items() if k != 'commands'}, indent=2))


if __name__ == '__main__':
    main()
