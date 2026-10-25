/**
 * registry_update_editor/cli_update_builder.js
 * Utility da riga di comando per creare, firmare e validare file "registry_update.update".
 * Esegue il Safety Check anti-instabilità in conformità con PIANO_UPDATE_SYSTEM.md.
 */

const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const HMAC_KEY = Buffer.from('NewSoft-NewOS-RegistryCertKey-26', 'ascii');

function isUnstableKey(keyPath, valName) {
    const full = (keyPath + '\\' + valName).toLowerCase();
    if (full.includes('user\\') || full.includes('current_user')) return true;
    if (full.includes('cpuvendor') || full.includes('memtotalmb')) return true;
    if (full.includes('\\irq') || full.endsWith('\\irq')) return true;
    if (full.includes('\\iobase') || full.endsWith('\\iobase')) return true;
    if (full.includes('ipaddress') || full.includes('macaddress')) return true;
    if (full.includes('hardware\\pci')) return true;
    return false;
}

function buildUpdatePackage(items, outputPath) {
    let sbPayload = '';
    let totalKeys = 0;
    let totalValues = 0;

    for (const item of items) {
        if (item.action === 'CREATE_KEY') totalKeys++;
        else totalValues++;
    }

    sbPayload += '[RegistryUpdate_Header]\r\n';
    sbPayload += `TotalKeys=${totalKeys}\r\n`;
    sbPayload += `TotalValues=${totalValues}\r\n`;
    sbPayload += 'SafetyCheckEnabled=1\r\n';
    sbPayload += 'MinBuildVersion=1.0.0\r\n';
    sbPayload += 'GeneratedBy=NewOS Registry Update CLI Builder (Node.js)\r\n\r\n';

    let idx = 0;
    for (const item of items) {
        const unstable = isUnstableKey(item.keyPath, item.valueName || '@');
        sbPayload += `[Update_Item_${idx++}]\r\n`;
        sbPayload += `Action=${item.action}\r\n`;
        sbPayload += `KeyPath=${item.keyPath}\r\n`;
        sbPayload += `ValueName=${item.valueName || '@'}\r\n`;
        sbPayload += `ValueType=${item.valueType || 'REG_SZ'}\r\n`;
        sbPayload += `NewData=${item.newData || ''}\r\n`;
        sbPayload += `StabilityClass=${unstable ? 'UNSTABLE_PROHIBITED' : 'STABLE'}\r\n\r\n`;
    }

    const payloadBuffer = Buffer.from(sbPayload, 'utf8');
    const payloadDigest = crypto.createHash('sha256').update(payloadBuffer).digest('hex').toUpperCase();

    const hmac = crypto.createHmac('sha256', HMAC_KEY);
    hmac.update(payloadBuffer);
    const signatureHex = hmac.digest('hex').toUpperCase();

    const certDate = new Date().toISOString();
    let sbCert = '';
    sbCert += '; ==============================================================================\r\n';
    sbCert += '; NEWOS V1 - REGISTRY DIFFERENTIAL UPDATE PACKAGE (.UPDATE)\r\n';
    sbCert += '; Generato da NewOS Registry Update CLI - Autorita\' NewSoft\r\n';
    sbCert += '; ==============================================================================\r\n';
    sbCert += '[NewOS_Registry_Certificate]\r\n';
    sbCert += `CertificateID=REG-AUTH-${Date.now()}-CLI\r\n`;
    sbCert += 'Issuer=Newspoint Software - NewSoft Root Authority\r\n';
    sbCert += 'TargetOS=NewOS V1 (64-bit)\r\n';
    sbCert += 'TargetRegistry=C:\\NewOS\\Registry\\REGISTRY.DAT\r\n';
    sbCert += `PayloadDigest=${payloadDigest}\r\n`;
    sbCert += `Timestamp=${certDate}\r\n`;
    sbCert += 'SignatureTag=NEWSOFT-REG-AUTH-SIGNATURE-V1\r\n';
    sbCert += `SignatureBytes=${signatureHex}\r\n\r\n`;

    const fullContent = sbCert + sbPayload;
    fs.writeFileSync(outputPath, fullContent, 'utf8');
    console.log(`[OK] Pacchetto firmato generato con successo: ${outputPath}`);
    console.log(`     Payload Digest: ${payloadDigest}`);
    console.log(`     Firma HMAC:     ${signatureHex.substring(0, 32)}...`);
    console.log(`     Totale chiavi:  ${totalKeys}, Valori: ${totalValues}`);
}

function verifyUpdatePackage(filePath) {
    if (!fs.existsSync(filePath)) {
        console.error(`[ERRORE] File non trovato: ${filePath}`);
        return false;
    }
    const content = fs.readFileSync(filePath, 'utf8');
    const headerIdx = content.indexOf('[RegistryUpdate_Header]');
    if (headerIdx === -1) {
        console.error('[ERRORE] Blocco [RegistryUpdate_Header] assente!');
        return false;
    }

    const certPart = content.substring(0, headerIdx);
    const payloadPart = content.substring(headerIdx);

    const sigMatch = certPart.match(/SignatureBytes=([0-9A-Fa-f]{64})/);
    const digestMatch = certPart.match(/PayloadDigest=([0-9A-Fa-f]{64})/);

    if (!sigMatch || !digestMatch) {
        console.error('[ERRORE] Certificato di autorizzazione corrotto o incompleto!');
        return false;
    }

    const expectedDigest = digestMatch[1].toUpperCase();
    const expectedSig = sigMatch[1].toUpperCase();

    const payloadBuffer = Buffer.from(payloadPart, 'utf8');
    const calcDigest = crypto.createHash('sha256').update(payloadBuffer).digest('hex').toUpperCase();

    const hmac = crypto.createHmac('sha256', HMAC_KEY);
    hmac.update(payloadBuffer);
    const calcSig = hmac.digest('hex').toUpperCase();

    if (calcDigest !== expectedDigest) {
        console.error('[FAIL] Payload digest alterato o corrotto!');
        return false;
    }

    if (calcSig !== expectedSig) {
        console.error('[FAIL] Firma crittografica non valida! Autorita\' NewSoft non riconosciuta.');
        return false;
    }

    console.log(`[VERIFICA OK] File integro e autentico: ${filePath}`);
    console.log('              Certificato NewSoft Root Authority VALIDO.');
    return true;
}

// Modalità CLI
if (require.main === module) {
    const args = process.argv.slice(2);
    if (args.length === 0 || args[0] === '--help') {
        console.log('Uso: node cli_update_builder.js --verify <file.update>');
        console.log('     node cli_update_builder.js --sample-build <output.update>');
        process.exit(0);
    }

    if (args[0] === '--verify' && args[1]) {
        const ok = verifyUpdatePackage(args[1]);
        process.exit(ok ? 0 : 1);
    } else if (args[0] === '--sample-build' && args[1]) {
        const sampleItems = [
            {
                action: 'SET_VALUE',
                keyPath: 'HKEY_LOCAL_MACHINE\\SYSTEM',
                valueName: 'ProductName',
                valueType: 'REG_SZ',
                newData: 'NewOS V1 64-bit Edition (Update 1)'
            },
            {
                action: 'SET_VALUE',
                keyPath: 'HKEY_LOCAL_MACHINE\\SYSTEM',
                valueName: 'BuildNumber',
                valueType: 'REG_DWORD',
                newData: '20261024'
            },
            {
                action: 'CREATE_KEY',
                keyPath: 'HKEY_LOCAL_MACHINE\\SOFTWARE\\NewSoft\\UpdateSystem',
                valueName: 'Version',
                valueType: 'REG_SZ',
                newData: '1.1.0'
            },
            {
                action: 'SET_VALUE',
                keyPath: 'HKEY_CLASSES_ROOT\\.mp4',
                valueName: 'OpenWith',
                valueType: 'REG_SZ',
                newData: 'C:\\NewOS\\SystemProgram\\PLAYER.EXE'
            }
        ];
        buildUpdatePackage(sampleItems, args[1]);
        process.exit(0);
    }
}

module.exports = { buildUpdatePackage, verifyUpdatePackage, isUnstableKey };
