#nullable enable
using System;
using System.Collections.Generic;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace SystemPackManagerApp
{
    public enum FileAuditStatus
    {
        Authentic,
        Modified,
        NewFile,
        Missing,
        Corrupted
    }

    public class FileAuditItem
    {
        public string RelPath { get; set; } = string.Empty;
        public long Size { get; set; }
        public uint Crc32 { get; set; }
        public string Crc32Hex => $"0x{Crc32:X8}";
        public string Sha256 { get; set; } = string.Empty;
        public FileAuditStatus Status { get; set; }
        public string Detail { get; set; } = string.Empty;
        public DateTime LastModified { get; set; }
    }

    public class ManifestData
    {
        [JsonPropertyName("package_info")]
        public PackageInfo Info { get; set; } = new();

        [JsonPropertyName("files")]
        public List<ManifestFileEntry> Files { get; set; } = new();
    }

    public class PackageInfo
    {
        [JsonPropertyName("magic")]
        public string Magic { get; set; } = "NOSPAK10";

        [JsonPropertyName("total_files")]
        public int TotalFiles { get; set; }

        [JsonPropertyName("total_uncompressed_bytes")]
        public long TotalUncompressedBytes { get; set; }

        [JsonPropertyName("encrypted_payload_crc32")]
        public string EncryptedPayloadCrc32 { get; set; } = string.Empty;

        [JsonPropertyName("timestamp")]
        public string Timestamp { get; set; } = string.Empty;

        [JsonPropertyName("extracted_from")]
        public string ExtractedFrom { get; set; } = string.Empty;
    }

    public class ManifestFileEntry
    {
        [JsonPropertyName("path")]
        public string Path { get; set; } = string.Empty;

        [JsonPropertyName("size")]
        public long Size { get; set; }

        [JsonPropertyName("crc32")]
        public string Crc32 { get; set; } = string.Empty;

        [JsonPropertyName("sha256")]
        public string Sha256 { get; set; } = string.Empty;

        [JsonPropertyName("status")]
        public string Status { get; set; } = "AUTHENTIC";
    }

    public static class Crc32
    {
        private static readonly uint[] Table;

        static Crc32()
        {
            Table = new uint[256];
            for (uint i = 0; i < 256; i++)
            {
                uint entry = i;
                for (int j = 0; j < 8; j++)
                {
                    entry = (entry & 1) != 0 ? (entry >> 1) ^ 0xEDB88320 : entry >> 1;
                }
                Table[i] = entry;
            }
        }

        public static uint Compute(byte[] buffer, int offset = 0, int count = -1)
        {
            if (count < 0) count = buffer.Length - offset;
            uint crc = 0xFFFFFFFF;
            for (int i = offset; i < offset + count; i++)
            {
                crc = (crc >> 8) ^ Table[(crc & 0xFF) ^ buffer[i]];
            }
            return ~crc;
        }

        public static uint ComputeFile(string filePath)
        {
            using var stream = File.OpenRead(filePath);
            byte[] buffer = new byte[65536];
            uint crc = 0xFFFFFFFF;
            int bytesRead;
            while ((bytesRead = stream.Read(buffer, 0, buffer.Length)) > 0)
            {
                for (int i = 0; i < bytesRead; i++)
                {
                    crc = (crc >> 8) ^ Table[(crc & 0xFF) ^ buffer[i]];
                }
            }
            return ~crc;
        }
    }

    public class PakEngine
    {
        public static readonly byte[] MasterKey = Encoding.UTF8.GetBytes("NewOS-V1SETUP-256BIT-AUTHENTIC26");
        public const string PakMagic = "NOSPAK10";

        public static string ComputeSha256(byte[] data)
        {
            byte[] hash = SHA256.HashData(data);
            return Convert.ToHexString(hash).ToLowerInvariant();
        }

        public static string ComputeFileSha256(string filePath)
        {
            using var stream = File.OpenRead(filePath);
            byte[] hash = SHA256.HashData(stream);
            return Convert.ToHexString(hash).ToLowerInvariant();
        }

        public static List<FileAuditItem> Unpack(string pakPath, string destDir, Action<string, bool> logCallback, Action<int, int>? progressCallback = null)
        {
            if (!File.Exists(pakPath))
                throw new FileNotFoundException($"Pacchetto non trovato: {pakPath}");

            byte[] fileBytes = File.ReadAllBytes(pakPath);
            if (fileBytes.Length < 64)
                throw new InvalidDataException("File troppo piccolo per contenere l'header NOSPAK10 (minimo 64 byte).");

            string magic = Encoding.ASCII.GetString(fileBytes, 0, 8);
            if (magic != PakMagic)
                throw new InvalidDataException($"Firma magica invalida: '{magic}'. Previsto '{PakMagic}'! Possibile manomissione.");

            uint headerSize = BitConverter.ToUInt32(fileBytes, 8);
            uint totalFiles = BitConverter.ToUInt32(fileBytes, 12);
            uint totalUncompressed = BitConverter.ToUInt32(fileBytes, 16);
            uint encryptedSize = BitConverter.ToUInt32(fileBytes, 20);
            byte[] iv = new byte[16];
            Array.Copy(fileBytes, 24, iv, 0, 16);
            uint expectedCrc32 = BitConverter.ToUInt32(fileBytes, 40);

            logCallback($"[HEADER] Magic: {magic} | File dichiarati: {totalFiles} | Dimensione decomp: {totalUncompressed / 1024.0 / 1024.0:F2} MB", false);
            logCallback($"[CIFRATURA] Algoritmo: AES-256-CBC | Dimensione Cifrata: {encryptedSize} B | IV: 0x{Convert.ToHexString(iv)}", false);

            if (encryptedSize % 16 != 0)
                throw new InvalidDataException($"Dimensione cifrata ({encryptedSize}) non conforme ai blocchi AES da 16 byte.");

            if (fileBytes.Length < 64 + encryptedSize)
                throw new InvalidDataException("Il file è incompleto o troncato rispetto alla dimensione dichiarata nell'header.");

            // Verifica CRC32 del ciphertext (Anti-Bruteforce / Tampering test)
            logCallback("--> [AUDIT] Verifica integrità crittografica del payload cifrato...", false);
            uint actualCrc32 = Crc32.Compute(fileBytes, 64, (int)encryptedSize);
            if (actualCrc32 != expectedCrc32)
            {
                throw new CryptographicException($"ALLARME MANOMISSIONE PAYLOAD!\nCRC32 calcolato (0x{actualCrc32:X8}) difforme da quello autentico (0x{expectedCrc32:X8}).\nIl ciphertext è stato manipolato o sottoposto a brute-force!");
            }
            logCallback($"[OK] Checksum CRC32 ciphertext confermato integro: 0x{actualCrc32:X8}", false);

            // Decifratura AES-256-CBC
            logCallback("--> [AES-256] Decifratura in corso con Master Key a 256 bit...", false);
            byte[] decrypted;
            using (var aes = Aes.Create())
            {
                aes.Key = MasterKey;
                aes.IV = iv;
                aes.Mode = CipherMode.CBC;
                aes.Padding = PaddingMode.None;

                using var decryptor = aes.CreateDecryptor();
                decrypted = decryptor.TransformFinalBlock(fileBytes, 64, (int)encryptedSize);
            }
            logCallback($"[OK] Decifratura completata con successo ({decrypted.Length} byte).", false);

            if (!Directory.Exists(destDir))
                Directory.CreateDirectory(destDir);

            int offset = 0;
            int extractedCount = 0;
            var auditList = new List<FileAuditItem>();
            var manifestEntries = new List<ManifestFileEntry>();

            while (offset < totalUncompressed && extractedCount < totalFiles)
            {
                if (offset + 2 > decrypted.Length) break;
                ushort pathLen = BitConverter.ToUInt16(decrypted, offset);
                offset += 2;

                if (offset + pathLen > decrypted.Length)
                    throw new InvalidDataException($"Record corrotto all'offset {offset}: lunghezza percorso eccede il payload.");

                string relPath = Encoding.UTF8.GetString(decrypted, offset, pathLen);
                offset += pathLen;

                uint fileSize = BitConverter.ToUInt32(decrypted, offset);
                offset += 4;

                uint fileCrc32 = BitConverter.ToUInt32(decrypted, offset);
                offset += 4;

                if (offset + fileSize > decrypted.Length)
                    throw new InvalidDataException($"Dati del file {relPath} eccedono la dimensione del buffer.");

                byte[] fileData = new byte[fileSize];
                Array.Copy(decrypted, offset, fileData, 0, fileSize);
                offset += (int)fileSize;

                // Calcolo e verifica checksum per singolo file
                uint actualFileCrc = Crc32.Compute(fileData);
                string sha256 = ComputeSha256(fileData);

                bool isMatch = (actualFileCrc == fileCrc32);
                if (!isMatch)
                {
                    logCallback($"[ATTENZIONE] Checksum fallito per {relPath}: 0x{actualFileCrc:X8} != 0x{fileCrc32:X8}", true);
                }

                string targetFilePath = Path.Combine(destDir, relPath.Replace('/', Path.DirectorySeparatorChar));
                string? parentDir = Path.GetDirectoryName(targetFilePath);
                if (!string.IsNullOrEmpty(parentDir) && !Directory.Exists(parentDir))
                    Directory.CreateDirectory(parentDir);

                File.WriteAllBytes(targetFilePath, fileData);

                var item = new FileAuditItem
                {
                    RelPath = relPath.Replace('\\', '/'),
                    Size = fileSize,
                    Crc32 = fileCrc32,
                    Sha256 = sha256,
                    Status = isMatch ? FileAuditStatus.Authentic : FileAuditStatus.Corrupted,
                    Detail = isMatch ? "Autentico e Integro" : "Checksum CRC32 non corrispondente!",
                    LastModified = DateTime.Now
                };
                auditList.Add(item);

                manifestEntries.Add(new ManifestFileEntry
                {
                    Path = item.RelPath,
                    Size = item.Size,
                    Crc32 = item.Crc32Hex,
                    Sha256 = item.Sha256,
                    Status = isMatch ? "AUTHENTIC" : "CORRUPTED"
                });

                extractedCount++;
                progressCallback?.Invoke(extractedCount, (int)totalFiles);
            }

            // Salvataggio manifest locale
            string manifestPath = Path.Combine(destDir, ".pack_manifest.json");
            var manifestObj = new ManifestData
            {
                Info = new PackageInfo
                {
                    Magic = PakMagic,
                    TotalFiles = extractedCount,
                    TotalUncompressedBytes = totalUncompressed,
                    EncryptedPayloadCrc32 = $"0x{actualCrc32:X8}",
                    Timestamp = DateTime.UtcNow.ToString("o"),
                    ExtractedFrom = Path.GetFullPath(pakPath)
                },
                Files = manifestEntries
            };
            string json = JsonSerializer.Serialize(manifestObj, new JsonSerializerOptions { WriteIndented = true });
            File.WriteAllText(manifestPath, json, Encoding.UTF8);

            logCallback($"[COMPLETATO] Estratti {extractedCount} file con successo in: {destDir}", false);
            return auditList;
        }

        public static List<FileAuditItem> AuditFolder(string workDir, string? pakPath = null, Action<string, bool>? logCallback = null)
        {
            var results = new List<FileAuditItem>();
            if (!Directory.Exists(workDir)) return results;

            // Caricamento manifest se disponibile
            string manifestPath = Path.Combine(workDir, ".pack_manifest.json");
            var manifestDict = new Dictionary<string, ManifestFileEntry>(StringComparer.OrdinalIgnoreCase);

            if (File.Exists(manifestPath))
            {
                try
                {
                    string json = File.ReadAllText(manifestPath, Encoding.UTF8);
                    var man = JsonSerializer.Deserialize<ManifestData>(json);
                    if (man?.Files != null)
                    {
                        foreach (var f in man.Files)
                        {
                            manifestDict[f.Path.Replace('\\', '/')] = f;
                        }
                    }
                }
                catch (Exception ex)
                {
                    logCallback?.Invoke($"[AVVISO MANIFEST] Errore lettura manifest: {ex.Message}", true);
                }
            }

            var diskFiles = ScanFiles(workDir);
            var seenInDisk = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

            foreach (var f in diskFiles)
            {
                string relNorm = f.RelPath.Replace('\\', '/');
                seenInDisk.Add(relNorm);

                uint crc = Crc32.ComputeFile(f.FullPath);
                string sha256 = ComputeFileSha256(f.FullPath);

                var item = new FileAuditItem
                {
                    RelPath = relNorm,
                    Size = f.Size,
                    Crc32 = crc,
                    Sha256 = sha256,
                    LastModified = File.GetLastWriteTime(f.FullPath)
                };

                if (manifestDict.TryGetValue(relNorm, out var orig))
                {
                    if (orig.Sha256.Equals(sha256, StringComparison.OrdinalIgnoreCase) && orig.Size == f.Size)
                    {
                        item.Status = FileAuditStatus.Authentic;
                        item.Detail = "Autentico (SHA256 e dimensione intatti)";
                    }
                    else
                    {
                        item.Status = FileAuditStatus.Modified;
                        item.Detail = $"Modificato rispetto al manifest originale (Orig SHA256: {orig.Sha256[..8]}...)";
                    }
                }
                else
                {
                    item.Status = FileAuditStatus.NewFile;
                    item.Detail = "Nuovo file aggiunto dall'utente (non presente in origine)";
                }

                results.Add(item);
            }

            // File mancanti
            foreach (var kvp in manifestDict)
            {
                if (!seenInDisk.Contains(kvp.Key))
                {
                    results.Add(new FileAuditItem
                    {
                        RelPath = kvp.Key,
                        Size = kvp.Value.Size,
                        Crc32 = Convert.ToUInt32(kvp.Value.Crc32, 16),
                        Sha256 = kvp.Value.Sha256,
                        Status = FileAuditStatus.Missing,
                        Detail = "File di sistema rimosso o assente nella cartella",
                        LastModified = DateTime.MinValue
                    });
                }
            }

            return results;
        }

        public static (int totalFiles, long uncompressed, long encrypted, uint payloadCrc) Pack(
            string workDir,
            string outPakPath,
            bool syncToBuild,
            Action<string, bool> logCallback,
            Action<int, int>? progressCallback = null)
        {
            var files = ScanFiles(workDir);
            if (files.Count == 0)
                throw new InvalidOperationException($"Nessun file trovato da impacchettare in: {workDir}");

            logCallback($"[PACK] Rilevati {files.Count} file da impacchettare e cifrare...", false);

            long totalUncompressed = 0;
            foreach (var f in files)
            {
                totalUncompressed += (2 + Encoding.UTF8.GetByteCount(f.RelPath) + 4 + 4 + f.Size);
            }

            byte[] plainBuffer = new byte[totalUncompressed];
            int offset = 0;
            var manifestEntries = new List<ManifestFileEntry>();

            for (int i = 0; i < files.Count; i++)
            {
                var f = files[i];
                byte[] fileBytes = File.ReadAllBytes(f.FullPath);
                byte[] pathBytes = Encoding.UTF8.GetBytes(f.RelPath);
                uint fileCrc = Crc32.Compute(fileBytes);
                string fileSha = ComputeSha256(fileBytes);

                // Record: path_len (uint16), path, size (uint32), crc32 (uint32), data
                BitConverter.GetBytes((ushort)pathBytes.Length).CopyTo(plainBuffer, offset);
                offset += 2;

                pathBytes.CopyTo(plainBuffer, offset);
                offset += pathBytes.Length;

                BitConverter.GetBytes((uint)fileBytes.Length).CopyTo(plainBuffer, offset);
                offset += 4;

                BitConverter.GetBytes(fileCrc).CopyTo(plainBuffer, offset);
                offset += 4;

                fileBytes.CopyTo(plainBuffer, offset);
                offset += fileBytes.Length;

                manifestEntries.Add(new ManifestFileEntry
                {
                    Path = f.RelPath,
                    Size = f.Size,
                    Crc32 = $"0x{fileCrc:X8}",
                    Sha256 = fileSha,
                    Status = "AUTHENTIC"
                });

                progressCallback?.Invoke(i + 1, files.Count);
            }

            // Padding a 16 byte per AES
            int padLen = 16 - (plainBuffer.Length % 16);
            if (padLen == 0) padLen = 16;
            byte[] fullPlain = new byte[plainBuffer.Length + padLen];
            Array.Copy(plainBuffer, fullPlain, plainBuffer.Length);
            for (int i = plainBuffer.Length; i < fullPlain.Length; i++)
                fullPlain[i] = (byte)padLen;

            // Generazione IV casuale sicuro 128 bit
            byte[] iv = new byte[16];
            RandomNumberGenerator.Fill(iv);

            // Cifratura AES-256-CBC
            byte[] encryptedPayload;
            using (var aes = Aes.Create())
            {
                aes.Key = MasterKey;
                aes.IV = iv;
                aes.Mode = CipherMode.CBC;
                aes.Padding = PaddingMode.None;

                using var encryptor = aes.CreateEncryptor();
                encryptedPayload = encryptor.TransformFinalBlock(fullPlain, 0, fullPlain.Length);
            }

            uint payloadCrc = Crc32.Compute(encryptedPayload);

            // Costruzione Header NOSPAK10 (64 byte)
            byte[] header = new byte[64];
            Encoding.ASCII.GetBytes(PakMagic).CopyTo(header, 0);
            BitConverter.GetBytes((uint)64).CopyTo(header, 8);
            BitConverter.GetBytes((uint)files.Count).CopyTo(header, 12);
            BitConverter.GetBytes((uint)totalUncompressed).CopyTo(header, 16);
            BitConverter.GetBytes((uint)encryptedPayload.Length).CopyTo(header, 20);
            iv.CopyTo(header, 24);
            BitConverter.GetBytes(payloadCrc).CopyTo(header, 40);

            // Scrittura file finale
            string? outDir = Path.GetDirectoryName(outPakPath);
            if (!string.IsNullOrEmpty(outDir) && !Directory.Exists(outDir))
                Directory.CreateDirectory(outDir);

            using (var outStream = File.Create(outPakPath))
            {
                outStream.Write(header, 0, header.Length);
                outStream.Write(encryptedPayload, 0, encryptedPayload.Length);
            }

            // Aggiorna manifest
            string manifestPath = Path.Combine(workDir, ".pack_manifest.json");
            var manifestObj = new ManifestData
            {
                Info = new PackageInfo
                {
                    Magic = PakMagic,
                    TotalFiles = files.Count,
                    TotalUncompressedBytes = totalUncompressed,
                    EncryptedPayloadCrc32 = $"0x{payloadCrc:X8}",
                    Timestamp = DateTime.UtcNow.ToString("o"),
                    ExtractedFrom = Path.GetFullPath(outPakPath)
                },
                Files = manifestEntries
            };
            File.WriteAllText(manifestPath, JsonSerializer.Serialize(manifestObj, new JsonSerializerOptions { WriteIndented = true }), Encoding.UTF8);

            logCallback($"[COMPLETATO] File SYSTEM.PAK generato con successo ({header.Length + encryptedPayload.Length:N0} byte)!", false);
            logCallback($"  File inclusi: {files.Count} | CRC32 Ciphertext: 0x{payloadCrc:X8}", false);

            if (syncToBuild)
            {
                try
                {
                    string root = Path.GetFullPath(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, @"..\..\..\..\"));
                    if (File.Exists(Path.Combine(root, "rules", "comportamenti.md")))
                    {
                        string buildPak1 = Path.Combine(root, "build", "bin", "BootSetup", "SYSTEM.PAK");
                        string buildPak2 = Path.Combine(root, "build", "SYSTEM.PAK");

                        if (Directory.Exists(Path.GetDirectoryName(buildPak1)))
                        {
                            File.Copy(outPakPath, buildPak1, true);
                            logCallback($"  [SYNC BUILD] Copiato in: {buildPak1}", false);
                        }
                        if (Directory.Exists(Path.GetDirectoryName(buildPak2)))
                        {
                            File.Copy(outPakPath, buildPak2, true);
                            logCallback($"  [SYNC BUILD] Copiato in: {buildPak2}", false);
                        }
                    }
                }
                catch (Exception ex)
                {
                    logCallback($"  [SYNC AVVISO] Impossibile sincronizzare in build: {ex.Message}", true);
                }
            }

            return (files.Count, totalUncompressed, header.Length + encryptedPayload.Length, payloadCrc);
        }

        public static List<(string FullPath, string RelPath, long Size)> ScanFiles(string dir, string baseDir = "")
        {
            var list = new List<(string, string, long)>();
            if (!Directory.Exists(dir)) return list;

            foreach (var item in Directory.GetFileSystemEntries(dir))
            {
                string name = Path.GetFileName(item);
                if (name == "SYSTEM.PAK" || name == ".pack_manifest.json" || name.EndsWith(".bat", StringComparison.OrdinalIgnoreCase) ||
                    name.EndsWith(".exe", StringComparison.OrdinalIgnoreCase) || name.EndsWith(".tmp", StringComparison.OrdinalIgnoreCase) ||
                    name.StartsWith('.') || name.Equals("node_modules", StringComparison.OrdinalIgnoreCase))
                {
                    continue;
                }

                string rel = string.IsNullOrEmpty(baseDir) ? name : $"{baseDir}/{name}";
                string relNorm = rel.Replace('\\', '/');

                // Esclusione profilo Morgan riservato al live
                if (relNorm.Equals("User/Morgan", StringComparison.OrdinalIgnoreCase) || relNorm.StartsWith("User/Morgan/", StringComparison.OrdinalIgnoreCase))
                    continue;

                if (Directory.Exists(item))
                {
                    list.AddRange(ScanFiles(item, relNorm));
                }
                else
                {
                    var fi = new FileInfo(item);
                    list.Add((item, relNorm, fi.Length));
                }
            }
            return list;
        }
    }
}
