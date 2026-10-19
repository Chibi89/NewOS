/**
 * tools/test_registry_logon_policy.js
 * Test di validazione architetturale e di sicurezza per:
 * 1. C:\NewOS\Registry\REGISTRY.DAT (Cifrato AES-256 CBC, schema nativo NewOS)
 * 2. C:\NewOS\Rules\ (Regole di sistema ufficiali 01-05)
 * 3. Politica di Logon pre-desktop (Sola Lettura vincolante) e modifica solo su Desktop
 */

const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const ROOT = path.resolve(__dirname, '..');
const regPath = path.join(ROOT, 'build', 'bin', 'NewOS', 'Registry', 'REGISTRY.DAT');
const rulesDir = path.join(ROOT, 'build', 'bin', 'NewOS', 'Rules');
const oldRegPath = path.join(ROOT, 'build', 'bin', 'NewOS', 'REGISTRY.DAT');

console.log('=== TEST 1: Verifica Posizione e Cifratura REGISTRY.DAT ===');
if (fs.existsSync(oldRegPath)) {
    console.error('FAIL: Il file REGISTRY.DAT e\' ancora presente nella radice C:\\NewOS\\ anziche\' in C:\\NewOS\\Registry\\');
    process.exit(1);
} else {
    console.log('[OK] Nessun file residuo in C:\\NewOS\\REGISTRY.DAT');
}

if (!fs.existsSync(regPath)) {
    console.error('FAIL: File REGISTRY.DAT mancante in C:\\NewOS\\Registry\\');
    process.exit(1);
}
console.log('[OK] File presente in C:\\NewOS\\Registry\\REGISTRY.DAT');

const regRaw = fs.readFileSync(regPath, 'utf8');
if (!regRaw.includes('[NEWOS-V1-ENCRYPTED-REGISTRY]')) {
    console.error('FAIL: Il file REGISTRY.DAT non contiene l\'envelope di sicurezza cifrato!');
    process.exit(1);
}
console.log('[OK] Header di protezione presente: [NEWOS-V1-ENCRYPTED-REGISTRY]');

const ivMatch = regRaw.match(/IV=([0-9A-Fa-f]{32})/);
const dataMatch = regRaw.match(/DATA=([0-9A-Fa-f]+)/);
if (!ivMatch || !dataMatch) {
    console.error('FAIL: Parametri IV o DATA cifrati non trovati nel file!');
    process.exit(1);
}

const iv = Buffer.from(ivMatch[1], 'hex');
const encrypted = Buffer.from(dataMatch[1], 'hex');
const KEY = Buffer.from("NewOS-V1SETUP-256BIT-AUTHENTIC26", "ascii");

const decipher = crypto.createDecipheriv('aes-256-cbc', KEY, iv);
let decrypted = decipher.update(encrypted);
decrypted = Buffer.concat([decrypted, decipher.final()]);
const plainText = decrypted.toString('utf8');

console.log('[OK] Decifratura AES-256 CBC completata con successo!');

// Verifica divieto assoluto di chiavi Windows / Microsoft
if (plainText.includes('Microsoft\\Windows') || plainText.includes('CurrentVersion')) {
    console.error('FAIL: Il registro contiene ancora rami esterni Microsoft\\Windows!');
    process.exit(1);
}
console.log('[OK] Nessuna chiave esterna Windows rilevata. Registro 100% nativo NewOS!');

// Verifica rami nativi NewOS
const requiredKeys = [
    '[HKEY_LOCAL_MACHINE\\NEWOS\\SYSTEM]',
    '[HKEY_LOCAL_MACHINE\\NEWOS\\POLICY]',
    '[HKEY_LOCAL_MACHINE\\NEWOS\\INSTALLER]',
    '[HKEY_CURRENT_USER\\NEWOS\\PREFERENCES]',
    'AppInstallPolicy=REG_SZ:UserAppsMustInstallToProgramFile',
    'SystemAppPolicy=REG_SZ:SystemToolsMustResideInSystemProgram',
    'RegistryEncryption=REG_SZ:AES-256-CBC-Encrypted',
    'AccessConstraint=REG_SZ:ClearTextRestrictedToRegedit'
];

for (const rk of requiredKeys) {
    if (!plainText.includes(rk)) {
        console.error(`FAIL: Chiave/valore richiesto non trovato: ${rk}`);
        process.exit(1);
    }
}
console.log('[OK] Tutte le chiavi native HKEY_LOCAL_MACHINE\\NEWOS\\* verificate con successo.');

console.log('\n=== TEST 2: Verifica Cartella C:\\NewOS\\Rules\\ e Regole Ufficiali ===');
const requiredRules = [
    '01_SYSTEM_HIERARCHY.TXT',
    '02_REGISTRY_SECURITY.TXT',
    '03_INSTALL_POLICY.TXT',
    '04_USER_PRIVILEGES.TXT',
    '05_NETWORK_AND_DEVICES.TXT',
    'LEGGIMI.TXT'
];

for (const rf of requiredRules) {
    const rfp = path.join(rulesDir, rf);
    if (!fs.existsSync(rfp)) {
        console.error(`FAIL: File delle regole mancante: ${rf}`);
        process.exit(1);
    }
    const rtxt = fs.readFileSync(rfp, 'utf8');
    if (rtxt.length < 50) {
        console.error(`FAIL: File delle regole vuoto o troppo corto: ${rf}`);
        process.exit(1);
    }
}
console.log(`[OK] Tutti i 6 file delle regole in C:\\NewOS\\Rules\\ presenti e convalidati.`);

console.log('\n=== TEST 3: Verifica Regola di Sola Lettura nel Codice C (registry.c e kernel.c) ===');
const regC = fs.readFileSync(path.join(ROOT, 'kernel', 'fs', 'registry.c'), 'utf8');
const kernelC = fs.readFileSync(path.join(ROOT, 'kernel', 'kernel.c'), 'utf8');

if (!regC.includes('g_registry_read_only = true')) {
    console.error('FAIL: g_registry_read_only deve essere true di default!');
    process.exit(1);
}
if (!regC.includes('registry_logon_load_readonly_preferences')) {
    console.error('FAIL: Funzione registry_logon_load_readonly_preferences non implementata!');
    process.exit(1);
}
if (!kernelC.includes('registry_logon_load_readonly_preferences();')) {
    console.error('FAIL: registry_logon_load_readonly_preferences non invocata post-credenziali!');
    process.exit(1);
}
if (!kernelC.includes('registry_set_read_only(false);')) {
    console.error('FAIL: registry_set_read_only(false) non invocata all\'avvio del desktop!');
    process.exit(1);
}

console.log('[OK] Politica di Sola Lettura pre-desktop e sblocco su Desktop verificata nel codice C!');
console.log('\n>>> TUTTI I TEST HANNO AVUTO ESITO POSITIVO (PASS) <<<');
