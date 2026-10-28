const path = require('path');
const { execSync } = require('child_process');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');

function findClangTools() {
    const candidates = [
        path.join(ROOT, 'tools', 'llvm-mingw', 'llvm-mingw-20260616-ucrt-x86_64', 'bin')
    ];
    const wingetPackages = path.join(process.env.LOCALAPPDATA || '', 'Microsoft', 'WinGet', 'Packages');
    if (fs.existsSync(wingetPackages)) {
        const pkgs = fs.readdirSync(wingetPackages);
        for (const p of pkgs) {
            if (p.includes('LLVM-MinGW') || p.includes('LLVM')) {
                const sub = path.join(wingetPackages, p);
                const subEntries = fs.readdirSync(sub);
                for (const se of subEntries) {
                    const binPath = path.join(sub, se, 'bin');
                    if (fs.existsSync(path.join(binPath, 'clang.exe'))) {
                        candidates.push(binPath);
                    }
                }
                if (fs.existsSync(path.join(sub, 'bin', 'clang.exe'))) {
                    candidates.push(path.join(sub, 'bin'));
                }
            }
        }
    }
    candidates.push('C:\\Program Files\\LLVM\\bin');
    for (const c of candidates) {
        if (fs.existsSync(path.join(c, 'clang.exe'))) return c;
    }
    return '';
}

const llvmDir = findClangTools();
const llvmSize = path.join(llvmDir, 'llvm-size.exe');
const llvmNm = path.join(llvmDir, 'llvm-nm.exe');
const kernelElf = path.join(ROOT, 'build', 'bin', 'NewOS', 'Boot', 'kernel.elf');

// Read symbols from kernel.elf to see what actually ended up in the kernel binary!
const nmOut = execSync(`"${llvmNm}" --size-sort --print-size "${kernelElf}"`).toString();
const lines = nmOut.trim().split('\n');

const symbols = [];
let totalCode = 0;
let totalRoData = 0;
let totalBss = 0;
let totalData = 0;

for (const line of lines) {
    const parts = line.trim().split(/\s+/);
    if (parts.length >= 4) {
        const addr = parts[0];
        const size = parseInt(parts[1], 16) || 0;
        const type = parts[2];
        const name = parts[3];
        symbols.push({ addr, size, type, name });
        if (type === 't' || type === 'T') totalCode += size;
        else if (type === 'r' || type === 'R') totalRoData += size;
        else if (type === 'b' || type === 'B') totalBss += size;
        else if (type === 'd' || type === 'D') totalData += size;
    }
}

console.log(`\n=== DIMENSIONI REALI IN KERNEL.BIN ===`);
console.log(`Codice (.text)       : ${totalCode} byte (~${(totalCode/1024).toFixed(1)} KB)`);
console.log(`Dati costanti (.rodata): ${totalRoData} byte (~${(totalRoData/1024).toFixed(1)} KB)`);
console.log(`Dati inizializzati (.data): ${totalData} byte (~${(totalData/1024).toFixed(1)} KB)`);
console.log(`TOTALE FLASH/FILE    : ${totalCode + totalRoData + totalData} byte`);
console.log(`BSS (in RAM a runtime): ${totalBss} byte (~${(totalBss/1024).toFixed(1)} KB)`);

// Raggruppamento per famiglia di funzioni / applicazione
const categories = {
    'Explorer (File Manager)': s => s.name.startsWith('explorer_') || s.name.startsWith('app_explorer_'),
    'Terminale / Prompt (cmd)': s => s.name.startsWith('cmd_') || s.name.startsWith('app_cmd_'),
    'Browser Web (webbrowser)': s => s.name.startsWith('browser_') || s.name.startsWith('app_browser_'),
    'OOBE (Setup Iniziale)': s => s.name.startsWith('oobe_') || s.name.startsWith('app_oobe_'),
    'Pannello di Controllo & Applet (display, devmgmt, sound, power, about)': s => s.name.startsWith('display_') || s.name.startsWith('devmgmt_') || s.name.startsWith('sound_props_') || s.name.startsWith('app_power_') || s.name.startsWith('app_display_') || s.name.startsWith('app_about_') || s.name.startsWith('app_control_panel_') || s.name.startsWith('cpanel_'),
    'Modern Settings Loader / Fallback': s => s.name.startsWith('settings_') || s.name.startsWith('app_settings_'),
    'Editor di Registro (regedit)': s => s.name.startsWith('regedit_') || s.name.startsWith('app_regedit_'),
    'Installer (Installazione OS)': s => s.name.startsWith('installer_') || s.name.startsWith('app_installer_') || s.name.startsWith('disk_install_'),
    'Player Multimediale (player)': s => s.name.startsWith('player_') || s.name.startsWith('app_player_'),
    'Autoplay & Notifiche (autoplay)': s => s.name.startsWith('autoplay_') || s.name.startsWith('app_autoplay_'),
    'Blocco Note (notepad)': s => s.name.startsWith('notepad_') || s.name.startsWith('app_notepad_'),
    'Calcolatrice (calculator)': s => s.name.startsWith('calc_') || s.name.startsWith('app_calc_'),
    'Rules Viewer (rules_viewer)': s => s.name.startsWith('rules_viewer_') || s.name.startsWith('app_rules_viewer_'),
    'Task Manager (taskmgr)': s => s.name.startsWith('taskmgr_') || s.name.startsWith('app_taskmgr_'),
    'WhatsNew (whatsnew)': s => s.name.startsWith('whatsnew_') || s.name.startsWith('app_whatsnew_'),
    'Boot Menu GUI': s => s.name.startsWith('boot_menu_'),
    'Network Stack (ETH, IP, TCP, UDP, DHCP, DNS, TLS, Socket)': s => s.name.startsWith('net_') || s.name.startsWith('eth_') || s.name.startsWith('ip_') || s.name.startsWith('tcp_') || s.name.startsWith('udp_') || s.name.startsWith('dhcp_') || s.name.startsWith('dns_') || s.name.startsWith('tls_') || s.name.startsWith('socket_') || s.name.startsWith('arp_') || s.name.startsWith('icmp_') || s.name.startsWith('e1000_') || s.name.startsWith('rtl8139_'),
    'USB Stack (UHCI, OHCI, XHCI, HID, MSC, HUB)': s => s.name.startsWith('usb_') || s.name.startsWith('uhci_') || s.name.startsWith('ohci_') || s.name.startsWith('xhci_'),
    'File System VFS (exFAT, NTFS, ISO9660, UDF, RAMFS)': s => s.name.startsWith('exfat_') || s.name.startsWith('vfs_') || s.name.startsWith('ntfs_') || s.name.startsWith('iso9660_') || s.name.startsWith('udf_') || s.name.startsWith('ramfs_'),
    'Window Manager & Desktop Core': s => s.name.startsWith('wm_') || s.name.startsWith('draw_window_') || s.name.startsWith('desktop_') || s.name.startsWith('ctx_') || s.name.startsWith('context_menu_'),
    'Grafica & Font (graphics, font)': s => s.name.startsWith('gfx_') || s.name.startsWith('font_')
};

const catTotals = {};
for (const c of Object.keys(categories)) catTotals[c] = { code: 0, rodata: 0, total: 0 };
let otherTotal = 0;

for (const s of symbols) {
    if (s.type === 'b' || s.type === 'B') continue; // escludi BSS
    let matched = false;
    for (const [c, fn] of Object.entries(categories)) {
        if (fn(s)) {
            catTotals[c].total += s.size;
            if (s.type === 't' || s.type === 'T') catTotals[c].code += s.size;
            else catTotals[c].rodata += s.size;
            matched = true;
            break;
        }
    }
    if (!matched) {
        otherTotal += s.size;
    }
}

console.log('\n=== SUDDIVISIONE SPAZIO OCCUPATO IN KERNEL.BIN PER COMPONENTE ===');
const sortedCats = Object.entries(catTotals).sort((a,b) => b[1].total - a[1].total);
for (const [name, val] of sortedCats) {
    if (val.total > 0) {
        console.log(`${name.padEnd(65)} : ${val.total.toString().padStart(6)} byte (~${(val.total/1024).toFixed(1)} KB)`);
    }
}
console.log(`${'Core Kernel / Driver / Memory / Arch / Altro'.padEnd(65)} : ${otherTotal.toString().padStart(6)} byte (~${(otherTotal/1024).toFixed(1)} KB)`);


// Analisi dimensioni file oggetto compilati
const objDir = path.join(ROOT, 'build', 'obj');
const objs = fs.readdirSync(objDir).filter(f => f.endsWith('.o'));
const objResults = [];

for (const o of objs) {
    try {
        const out = execSync(`"${llvmSize}" "${path.join(objDir, o)}"`).toString();
        const olines = out.trim().split('\n');
        if (olines.length >= 2) {
            const parts = olines[1].trim().split(/\s+/);
            const text = parseInt(parts[0], 10) || 0;
            const data = parseInt(parts[1], 10) || 0;
            const bss = parseInt(parts[2], 10) || 0;
            objResults.push({ name: o, text, data, bss, total: text + data });
        }
    } catch(e) {}
}

objResults.sort((a,b) => b.total - a.total);
console.log('\n=== TOP 35 FILE OGGETTO COMPILATI (TEXT + DATA) ===');
for (let i = 0; i < Math.min(objResults.length, 35); i++) {
    const r = objResults[i];
    console.log(`${r.name.padEnd(25)} : total=${r.total.toString().padStart(6)} (text=${r.text.toString().padStart(6)}, data=${r.data.toString().padStart(6)})`);
}
