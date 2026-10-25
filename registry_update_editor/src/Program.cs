using System;
using System.IO;
using System.Text;
using System.Drawing;
using System.Windows.Forms;
using System.Security.Cryptography;
using System.Collections.Generic;

namespace NewOS.RegistryUpdateEditor
{
    // =========================================================================
    // Modello Dati e Strutture di Registro
    // =========================================================================
    public enum RegValueType
    {
        REG_SZ,
        REG_DWORD,
        REG_BINARY
    }

    public enum StabilityLevel
    {
        STABLE,
        UNSTABLE_PROHIBITED
    }

    public enum UpdateItemAction
    {
        ORIGINAL,
        CREATE_KEY,
        SET_VALUE
    }

    public class RegistryValueItem
    {
        public string Name { get; set; }
        public RegValueType Type { get; set; }
        public string Data { get; set; }
        public string OriginalData { get; set; }
        public UpdateItemAction Action { get; set; }
        public StabilityLevel Stability { get; set; }
        public string KeyPath { get; set; }

        public RegistryValueItem(string keyPath, string name, RegValueType type, string data, bool isOriginal = true)
        {
            KeyPath = keyPath;
            Name = name;
            Type = type;
            Data = data;
            OriginalData = isOriginal ? data : null;
            Action = isOriginal ? UpdateItemAction.ORIGINAL : UpdateItemAction.SET_VALUE;
            Stability = SafetyChecker.Evaluate(keyPath, name);
        }
    }

    public class RegistryNodeItem
    {
        public string FullPath { get; set; }
        public string Name { get; set; }
        public bool IsNewKey { get; set; }
        public Dictionary<string, RegistryValueItem> Values { get; set; }
        public Dictionary<string, RegistryNodeItem> SubKeys { get; set; }

        public RegistryNodeItem(string fullPath, string name, bool isNew = false)
        {
            FullPath = fullPath;
            Name = name;
            IsNewKey = isNew;
            Values = new Dictionary<string, RegistryValueItem>(StringComparer.OrdinalIgnoreCase);
            SubKeys = new Dictionary<string, RegistryNodeItem>(StringComparer.OrdinalIgnoreCase);
        }
    }

    // =========================================================================
    // Matrice di Sicurezza e Safety Check
    // =========================================================================
    public static class SafetyChecker
    {
        public static StabilityLevel Evaluate(string keyPath, string valueName)
        {
            string full = (keyPath + "\\" + valueName).Trim('\\');
            string upper = full.ToUpperInvariant();

            // Regola 1: Inviolabilità User e profili
            if (upper.Contains("USER\\") || upper.StartsWith("HKEY_CURRENT_USER") || upper.Contains("CURRENT_USER"))
                return StabilityLevel.UNSTABLE_PROHIBITED;

            // Risorse hardware dinamiche BIOS/PCI/DMA/IRQ/RAM
            if (upper.Contains("CPUVENDOR") ||
                upper.Contains("MEMTOTALMB") ||
                upper.Contains("\\IRQ") ||
                upper.EndsWith("\\IRQ") ||
                upper.Contains("\\IOBASE") ||
                upper.EndsWith("\\IOBASE") ||
                upper.Contains("IPADDRESS") ||
                upper.Contains("MACADDRESS") ||
                upper.Contains("HARDWARE\\PCI"))
            {
                return StabilityLevel.UNSTABLE_PROHIBITED;
            }

            return StabilityLevel.STABLE;
        }

        public static string GetExplanation(StabilityLevel level)
        {
            if (level == StabilityLevel.UNSTABLE_PROHIBITED)
                return "CRITICO / INSTABILE: Risorsa HW dinamica o profilo protetto. Modifica non consentita da UPDATE.MOD.";
            return "STABILE: Parametro sicuro. Autorizzato per aggiornamento differenziale con fallback automatico.";
        }
    }

    // =========================================================================
    // Parser e Crittografia AES-256 di REGISTRY.DAT
    // =========================================================================
    public class RegistryDatabase
    {
        public static readonly byte[] AES_MASTER_KEY = Encoding.ASCII.GetBytes("NewOS-V1SETUP-256BIT-AUTHENTIC26");
        public static readonly byte[] HMAC_CERT_KEY = Encoding.ASCII.GetBytes("NewSoft-NewOS-RegistryCertKey-26");

        public Dictionary<string, RegistryNodeItem> RootKeys { get; private set; }
        public bool IsEncryptedSource { get; private set; }
        public string SourcePath { get; private set; }
        public int TotalKeysCount { get; private set; }
        public int TotalValuesCount { get; private set; }

        public RegistryDatabase()
        {
            RootKeys = new Dictionary<string, RegistryNodeItem>(StringComparer.OrdinalIgnoreCase);
            EnsureRoot("HKEY_LOCAL_MACHINE");
            EnsureRoot("HKEY_CLASSES_ROOT");
            EnsureRoot("HKEY_CURRENT_USER");
        }

        private RegistryNodeItem EnsureRoot(string rootName)
        {
            if (!RootKeys.ContainsKey(rootName))
                RootKeys[rootName] = new RegistryNodeItem(rootName, rootName, false);
            return RootKeys[rootName];
        }

        public RegistryNodeItem FindOrCreateNode(string fullPath, bool isNew = false)
        {
            if (string.IsNullOrWhiteSpace(fullPath)) return null;
            string[] parts = fullPath.Replace('/', '\\').Trim('\\').Split('\\');
            if (parts.Length == 0) return null;

            RegistryNodeItem current = EnsureRoot(parts[0]);
            string currentPath = parts[0];

            for (int i = 1; i < parts.Length; i++)
            {
                string part = parts[i];
                currentPath += "\\" + part;
                if (!current.SubKeys.ContainsKey(part))
                {
                    current.SubKeys[part] = new RegistryNodeItem(currentPath, part, isNew);
                }
                current = current.SubKeys[part];
                if (isNew) current.IsNewKey = true;
            }

            return current;
        }

        public void LoadFromFile(string path)
        {
            SourcePath = path;
            RootKeys.Clear();
            EnsureRoot("HKEY_LOCAL_MACHINE");
            EnsureRoot("HKEY_CLASSES_ROOT");
            EnsureRoot("HKEY_CURRENT_USER");

            string rawText = File.ReadAllText(path, Encoding.UTF8);
            string plainText;

            if (rawText.Contains("[NEWOS-V1-ENCRYPTED-REGISTRY]"))
            {
                IsEncryptedSource = true;
                plainText = DecryptEnvelope(rawText);
            }
            else
            {
                IsEncryptedSource = false;
                plainText = rawText;
            }

            ParseIniContent(plainText);
        }

        private string DecryptEnvelope(string raw)
        {
            string ivHex = ExtractParameter(raw, "IV");
            string dataHex = ExtractParameter(raw, "DATA");

            if (string.IsNullOrEmpty(ivHex) || string.IsNullOrEmpty(dataHex))
                throw new InvalidDataException("Envelope REGISTRY.DAT corrotto: IV o DATA mancanti.");

            byte[] iv = HexToBytes(ivHex);
            byte[] cipher = HexToBytes(dataHex);

            using (Aes aes = Aes.Create())
            {
                aes.Key = AES_MASTER_KEY;
                aes.IV = iv;
                aes.Mode = CipherMode.CBC;
                aes.Padding = PaddingMode.PKCS7;

                using (ICryptoTransform decryptor = aes.CreateDecryptor())
                using (MemoryStream ms = new MemoryStream(cipher))
                using (CryptoStream cs = new CryptoStream(ms, decryptor, CryptoStreamMode.Read))
                using (StreamReader sr = new StreamReader(cs, Encoding.UTF8))
                {
                    return sr.ReadToEnd();
                }
            }
        }

        private static string ExtractParameter(string text, string param)
        {
            using (StringReader reader = new StringReader(text))
            {
                string line;
                while ((line = reader.ReadLine()) != null)
                {
                    line = line.Trim();
                    if (line.StartsWith(param + "=", StringComparison.OrdinalIgnoreCase))
                    {
                        return line.Substring(param.Length + 1).Trim();
                    }
                }
            }
            return null;
        }

        private static byte[] HexToBytes(string hex)
        {
            hex = hex.Trim();
            byte[] bytes = new byte[hex.Length / 2];
            for (int i = 0; i < bytes.Length; i++)
            {
                bytes[i] = Convert.ToByte(hex.Substring(i * 2, 2), 16);
            }
            return bytes;
        }

        private void ParseIniContent(string text)
        {
            TotalKeysCount = 0;
            TotalValuesCount = 0;
            string currentSection = "HKEY_LOCAL_MACHINE\\SYSTEM";

            using (StringReader sr = new StringReader(text))
            {
                string line;
                while ((line = sr.ReadLine()) != null)
                {
                    line = line.Trim();
                    if (string.IsNullOrEmpty(line) || line.StartsWith(";") || line.StartsWith("#") || line.StartsWith("//"))
                        continue;

                    if (line.StartsWith("[") && line.EndsWith("]"))
                    {
                        currentSection = line.Substring(1, line.Length - 2).Trim();
                        FindOrCreateNode(currentSection, false);
                        TotalKeysCount++;
                        continue;
                    }

                    int eqIdx = line.IndexOf('=');
                    if (eqIdx > 0)
                    {
                        string valName = line.Substring(0, eqIdx).Trim();
                        string rawVal = line.Substring(eqIdx + 1).Trim();

                        RegValueType vType = RegValueType.REG_SZ;
                        string valData = rawVal;

                        if (rawVal.StartsWith("REG_SZ:", StringComparison.OrdinalIgnoreCase))
                        {
                            vType = RegValueType.REG_SZ;
                            valData = rawVal.Substring(7);
                        }
                        else if (rawVal.StartsWith("REG_DWORD:", StringComparison.OrdinalIgnoreCase))
                        {
                            vType = RegValueType.REG_DWORD;
                            valData = rawVal.Substring(10);
                        }
                        else if (rawVal.StartsWith("REG_BINARY:", StringComparison.OrdinalIgnoreCase))
                        {
                            vType = RegValueType.REG_BINARY;
                            valData = rawVal.Substring(11);
                        }

                        RegistryNodeItem node = FindOrCreateNode(currentSection, false);
                        if (node != null)
                        {
                            node.Values[valName] = new RegistryValueItem(currentSection, valName, vType, valData, true);
                            TotalValuesCount++;
                        }
                    }
                }
            }
        }
    }

    // =========================================================================
    // Generatore e Firmatore registry_update.update
    // =========================================================================
    public static class UpdatePackageGenerator
    {
        public static string GenerateUpdateScript(List<RegistryValueItem> modifiedValues, List<string> newKeyPaths)
        {
            StringBuilder sbPayload = new StringBuilder();

            int totalKeys = newKeyPaths.Count;
            int totalValues = modifiedValues.Count;

            sbPayload.AppendLine("[RegistryUpdate_Header]");
            sbPayload.AppendLine("TotalKeys=" + totalKeys);
            sbPayload.AppendLine("TotalValues=" + totalValues);
            sbPayload.AppendLine("SafetyCheckEnabled=1");
            sbPayload.AppendLine("MinBuildVersion=1.0.0");
            sbPayload.AppendLine("GeneratedBy=NewOS Registry Update Editor (Windows GUI)");
            sbPayload.AppendLine();

            int itemIndex = 0;

            // Inclusione nuove chiavi
            foreach (string kp in newKeyPaths)
            {
                sbPayload.AppendLine(string.Format("[Update_Item_{0}]", itemIndex++));
                sbPayload.AppendLine("Action=CREATE_KEY");
                sbPayload.AppendLine("KeyPath=" + kp);
                sbPayload.AppendLine("ValueName=@");
                sbPayload.AppendLine("ValueType=REG_SZ");
                sbPayload.AppendLine("NewData=");
                sbPayload.AppendLine("StabilityClass=" + SafetyChecker.Evaluate(kp, "@").ToString());
                sbPayload.AppendLine();
            }

            // Inclusione valori aggiunti o modificati
            foreach (var val in modifiedValues)
            {
                sbPayload.AppendLine(string.Format("[Update_Item_{0}]", itemIndex++));
                sbPayload.AppendLine("Action=" + (val.Action == UpdateItemAction.CREATE_KEY ? "CREATE_KEY" : "SET_VALUE"));
                sbPayload.AppendLine("KeyPath=" + val.KeyPath);
                sbPayload.AppendLine("ValueName=" + val.Name);
                sbPayload.AppendLine("ValueType=" + val.Type.ToString());
                sbPayload.AppendLine("NewData=" + val.Data);
                sbPayload.AppendLine("StabilityClass=" + val.Stability.ToString());
                sbPayload.AppendLine();
            }

            string payloadString = sbPayload.ToString();

            // Calcolo SHA-256 del payload
            byte[] payloadBytes = Encoding.UTF8.GetBytes(payloadString);
            string payloadDigest;
            using (SHA256 sha = SHA256.Create())
            {
                byte[] hash = sha.ComputeHash(payloadBytes);
                payloadDigest = BitConverter.ToString(hash).Replace("-", "").ToUpperInvariant();
            }

            // Calcolo Firma HMAC-SHA256 con Master Key NewSoft
            string signatureHex;
            using (HMACSHA256 hmac = new HMACSHA256(RegistryDatabase.HMAC_CERT_KEY))
            {
                byte[] sigBytes = hmac.ComputeHash(payloadBytes);
                signatureHex = BitConverter.ToString(sigBytes).Replace("-", "").ToUpperInvariant();
            }

            // Intestazione Certificato Digitale di Autorizzazione
            StringBuilder sbCert = new StringBuilder();
            sbCert.AppendLine("; ==============================================================================");
            sbCert.AppendLine("; NEWOS V1 - REGISTRY DIFFERENTIAL UPDATE PACKAGE (.UPDATE)");
            sbCert.AppendLine("; Generato da NewOS Registry Update Editor - Autorita' di Rilascio NewSoft");
            sbCert.AppendLine("; ==============================================================================");
            sbCert.AppendLine("[NewOS_Registry_Certificate]");
            sbCert.AppendLine("CertificateID=REG-AUTH-" + DateTime.UtcNow.ToString("yyyyMMdd-HHmmss") + "-V1");
            sbCert.AppendLine("Issuer=Newspoint Software - NewSoft Root Authority");
            sbCert.AppendLine("TargetOS=NewOS V1 (64-bit)");
            sbCert.AppendLine("TargetRegistry=C:\\NewOS\\Registry\\REGISTRY.DAT");
            sbCert.AppendLine("PayloadDigest=" + payloadDigest);
            sbCert.AppendLine("Timestamp=" + DateTime.UtcNow.ToString("s") + "Z");
            sbCert.AppendLine("SignatureTag=NEWSOFT-REG-AUTH-SIGNATURE-V1");
            sbCert.AppendLine("SignatureBytes=" + signatureHex);
            sbCert.AppendLine();

            return sbCert.ToString() + payloadString;
        }
    }

    // =========================================================================
    // Dialog Inserimento / Modifica Valore
    // =========================================================================
    public class EditValueForm : Form
    {
        public string ValueName { get; private set; }
        public RegValueType ValueType { get; private set; }
        public string ValueData { get; private set; }

        private TextBox txtName;
        private ComboBox cmbType;
        private TextBox txtData;
        private Label lblWarning;

        public EditValueForm(string keyPath, string initialName = "", RegValueType initialType = RegValueType.REG_SZ, string initialData = "", bool isEditing = false)
        {
            this.Text = isEditing ? "Modifica Valore Registro - " + initialName : "Nuovo Valore Registro";
            this.Size = new Size(540, 360);
            this.StartPosition = FormStartPosition.CenterParent;
            this.FormBorderStyle = FormBorderStyle.FixedDialog;
            this.MaximizeBox = false;
            this.MinimizeBox = false;
            this.Font = new Font("Segoe UI", 9F, FontStyle.Regular);
            this.BackColor = Color.FromArgb(245, 247, 250);

            Label lblPath = new Label()
            {
                Text = "Percorso Chiave:\n" + keyPath,
                Location = new Point(20, 15),
                Size = new Size(480, 35),
                ForeColor = Color.FromArgb(50, 60, 75),
                Font = new Font("Segoe UI", 8.5F, FontStyle.Bold)
            };
            this.Controls.Add(lblPath);

            Label lblN = new Label() { Text = "Nome Valore:", Location = new Point(20, 60), AutoSize = true };
            this.Controls.Add(lblN);

            txtName = new TextBox()
            {
                Text = initialName,
                Location = new Point(20, 80),
                Size = new Size(480, 25),
                ReadOnly = isEditing
            };
            this.Controls.Add(txtName);

            Label lblT = new Label() { Text = "Tipo di Dato:", Location = new Point(20, 115), AutoSize = true };
            this.Controls.Add(lblT);

            cmbType = new ComboBox()
            {
                Location = new Point(20, 135),
                Size = new Size(480, 25),
                DropDownStyle = ComboBoxStyle.DropDownList
            };
            cmbType.Items.AddRange(new object[] { "REG_SZ (Stringa Testuale)", "REG_DWORD (Numero 32-bit)", "REG_BINARY (Esadecimale)" });
            cmbType.SelectedIndex = (int)initialType;
            this.Controls.Add(cmbType);

            Label lblD = new Label() { Text = "Dati del Valore:", Location = new Point(20, 170), AutoSize = true };
            this.Controls.Add(lblD);

            txtData = new TextBox()
            {
                Text = initialData,
                Location = new Point(20, 190),
                Size = new Size(480, 25)
            };
            this.Controls.Add(txtData);

            lblWarning = new Label()
            {
                Location = new Point(20, 225),
                Size = new Size(480, 45),
                ForeColor = Color.DarkRed,
                Font = new Font("Segoe UI", 8F, FontStyle.Regular)
            };
            this.Controls.Add(lblWarning);

            txtName.TextChanged += (s, e) => UpdateSafetyStatus(keyPath);
            UpdateSafetyStatus(keyPath);

            Button btnOk = new Button()
            {
                Text = "Conferma Modifica",
                DialogResult = DialogResult.OK,
                Location = new Point(230, 280),
                Size = new Size(130, 32),
                BackColor = Color.FromArgb(24, 119, 242),
                ForeColor = Color.White,
                FlatStyle = FlatStyle.Flat
            };
            btnOk.Click += (s, e) =>
            {
                if (string.IsNullOrWhiteSpace(txtName.Text))
                {
                    MessageBox.Show("Il nome del valore non può essere vuoto.", "Attenzione", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                    this.DialogResult = DialogResult.None;
                    return;
                }
                ValueName = txtName.Text.Trim();
                ValueType = (RegValueType)cmbType.SelectedIndex;
                ValueData = txtData.Text.Trim();
            };
            this.Controls.Add(btnOk);

            Button btnCancel = new Button()
            {
                Text = "Annulla",
                DialogResult = DialogResult.Cancel,
                Location = new Point(370, 280),
                Size = new Size(130, 32),
                BackColor = Color.FromArgb(225, 230, 235),
                FlatStyle = FlatStyle.Flat
            };
            this.Controls.Add(btnCancel);
            this.AcceptButton = btnOk;
            this.CancelButton = btnCancel;
        }

        private void UpdateSafetyStatus(string keyPath)
        {
            var safety = SafetyChecker.Evaluate(keyPath, txtName.Text.Trim());
            if (safety == StabilityLevel.UNSTABLE_PROHIBITED)
            {
                lblWarning.ForeColor = Color.Crimson;
                lblWarning.Text = "ATTENZIONE SAFETY CHECK: Questa chiave/valore e' classificata come INSTABILE/PROIBITA.\nUPDATE.MOD rifiutera' la scrittura per prevenire corruzioni hardware/utente.";
            }
            else
            {
                lblWarning.ForeColor = Color.DarkGreen;
                lblWarning.Text = "SAFETY CHECK: Chiave/Valore classificata come STABILE.\nAutorizzata per inclusione in registry_update.update con backup e fallback atomico.";
            }
        }
    }

    // =========================================================================
    // Finestra Principale NewOS Registry Update Editor
    // =========================================================================
    public class MainForm : Form
    {
        private RegistryDatabase db;
        private TreeView treeRegistry;
        private ListView listValues;
        private StatusStrip statusBar;
        private ToolStripStatusLabel lblStatusFile;
        private ToolStripStatusLabel lblStatusCrypto;
        private ToolStripStatusLabel lblStatusModified;

        private List<RegistryValueItem> modifiedValuesList = new List<RegistryValueItem>();
        private List<string> newKeyPathsList = new List<string>();

        private SplitContainer split;
        private MenuStrip menu;
        private ToolStrip toolBar;

        public MainForm()
        {
            db = new RegistryDatabase();

            this.Text = "NewOS Registry Update Editor - Suite di Manutenzione e Firma (Windows)";
            this.Size = new Size(1100, 700);
            this.MinimumSize = new Size(800, 500);
            this.StartPosition = FormStartPosition.CenterScreen;
            this.Font = new Font("Segoe UI", 9F, FontStyle.Regular);
            this.Icon = SystemIcons.Shield;

            BuildMenuAndToolbar();
            BuildMainLayout();
            BuildStatusBar();
            SetupZOrder();

            this.Load += (s, e) =>
            {
                split.FixedPanel = FixedPanel.Panel1;
                split.Panel1MinSize = 220;
                split.Panel2MinSize = 450;
                split.SplitterDistance = 280;
                AdjustListViewColumns();

                // Tentativo caricamento automatico da percorsi tipici del repository
                string defaultRepoReg = Path.GetFullPath(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "..", "build", "bin", "NewOS", "Registry", "REGISTRY.DAT"));
                if (File.Exists(defaultRepoReg))
                {
                    LoadRegistryFile(defaultRepoReg);
                }
                else
                {
                    string localReg = Path.GetFullPath(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "REGISTRY.DAT"));
                    if (File.Exists(localReg))
                        LoadRegistryFile(localReg);
                }
            };
        }

        private void SetupZOrder()
        {
            // In Windows Forms l'ordine di layout per i controlli Docked e':
            // 1. split (Fill)
            // 2. toolBar (Top)
            // 3. menu (Top)
            // 4. statusBar (Bottom)
            this.Controls.Add(split);
            this.Controls.Add(toolBar);
            this.Controls.Add(menu);
            this.Controls.Add(statusBar);

            menu.BringToFront();
            toolBar.BringToFront();
            statusBar.SendToBack();
            split.BringToFront();
        }

        private void BuildMenuAndToolbar()
        {
            menu = new MenuStrip();
            menu.Dock = DockStyle.Top;
            menu.BackColor = Color.FromArgb(246, 248, 250);

            ToolStripMenuItem menuFile = new ToolStripMenuItem("&File");
            ToolStripMenuItem mOpen = new ToolStripMenuItem("&Apri REGISTRY.DAT...", null, (s, e) => OnOpenFile());
            ToolStripMenuItem mExport = new ToolStripMenuItem("&Esporta e Firma registry_update.update...", null, (s, e) => OnExportUpdate());
            ToolStripMenuItem mExit = new ToolStripMenuItem("&Esci", null, (s, e) => this.Close());

            menuFile.DropDownItems.AddRange(new ToolStripItem[] { mOpen, new ToolStripSeparator(), mExport, new ToolStripSeparator(), mExit });

            ToolStripMenuItem menuEdit = new ToolStripMenuItem("&Modifica");
            ToolStripMenuItem mNewKey = new ToolStripMenuItem("&Nuova Chiave...", null, (s, e) => OnNewKey());
            ToolStripMenuItem mNewVal = new ToolStripMenuItem("Nuovo &Valore...", null, (s, e) => OnNewValue());
            ToolStripMenuItem mEditVal = new ToolStripMenuItem("&Modifica Valore Selezionato...", null, (s, e) => OnEditSelectedValue());
            ToolStripMenuItem mResetMod = new ToolStripMenuItem("&Azzera Modifiche Update", null, (s, e) => OnResetModifications());
            menuEdit.DropDownItems.AddRange(new ToolStripItem[] { mNewKey, mNewVal, mEditVal, new ToolStripSeparator(), mResetMod });

            ToolStripMenuItem menuHelp = new ToolStripMenuItem("&Aiuto");
            ToolStripMenuItem mAbout = new ToolStripMenuItem("&Informazioni su Registry Update Editor...", null, (s, e) => ShowAbout());
            menuHelp.DropDownItems.Add(mAbout);

            menu.Items.AddRange(new ToolStripItem[] { menuFile, menuEdit, menuHelp });
            this.MainMenuStrip = menu;

            // Barra degli strumenti superiore
            toolBar = new ToolStrip();
            toolBar.Dock = DockStyle.Top;
            toolBar.ImageScalingSize = new Size(18, 18);
            toolBar.GripStyle = ToolStripGripStyle.Hidden;
            toolBar.BackColor = Color.FromArgb(238, 242, 246);
            toolBar.Padding = new Padding(4, 2, 4, 2);

            ToolStripButton btnOpen = new ToolStripButton("  Apri REGISTRY.DAT  ", null, (s, e) => OnOpenFile());
            ToolStripButton btnNewKey = new ToolStripButton("+ Nuova Chiave", null, (s, e) => OnNewKey());
            ToolStripButton btnNewVal = new ToolStripButton("+ Nuovo Valore", null, (s, e) => OnNewValue());
            ToolStripButton btnEditVal = new ToolStripButton("Modifica Valore", null, (s, e) => OnEditSelectedValue());
            ToolStripSeparator sep = new ToolStripSeparator();
            ToolStripButton btnExport = new ToolStripButton("ESPORTA & FIRMA 'registry_update.update'", null, (s, e) => OnExportUpdate())
            {
                Font = new Font("Segoe UI", 9F, FontStyle.Bold),
                ForeColor = Color.FromArgb(12, 108, 230)
            };

            toolBar.Items.AddRange(new ToolStripItem[] { btnOpen, new ToolStripSeparator(), btnNewKey, btnNewVal, btnEditVal, sep, btnExport });
        }

        private void BuildMainLayout()
        {
            split = new SplitContainer();
            split.Dock = DockStyle.Fill;
            split.SplitterWidth = 6;
            split.BackColor = Color.FromArgb(220, 225, 230);

            // Albero Sinistro (TreeView)
            Panel pnlLeft = new Panel() { Dock = DockStyle.Fill, Padding = new Padding(6), BackColor = Color.White };
            Label lblTreeTitle = new Label()
            {
                Text = "Gerarchia Rami Registro (NewOS):",
                Dock = DockStyle.Top,
                Height = 26,
                Font = new Font("Segoe UI", 9F, FontStyle.Bold),
                ForeColor = Color.FromArgb(50, 65, 85),
                TextAlign = ContentAlignment.MiddleLeft
            };
            treeRegistry = new TreeView()
            {
                Dock = DockStyle.Fill,
                BorderStyle = BorderStyle.FixedSingle,
                HideSelection = false,
                ShowLines = true,
                FullRowSelect = false,
                Font = new Font("Segoe UI", 9F, FontStyle.Regular)
            };
            treeRegistry.AfterSelect += (s, e) => DisplayValuesForNode(e.Node.Tag as RegistryNodeItem);

            pnlLeft.Controls.Add(treeRegistry);
            pnlLeft.Controls.Add(lblTreeTitle);
            lblTreeTitle.BringToFront();
            treeRegistry.BringToFront();
            split.Panel1.Controls.Add(pnlLeft);

            // Lista Valori Destra (ListView)
            Panel pnlRight = new Panel() { Dock = DockStyle.Fill, Padding = new Padding(6), BackColor = Color.White };
            Label lblListTitle = new Label()
            {
                Text = "Valori Chiave e Safety Check (Modalita' Update):",
                Dock = DockStyle.Top,
                Height = 26,
                Font = new Font("Segoe UI", 9F, FontStyle.Bold),
                ForeColor = Color.FromArgb(50, 65, 85),
                TextAlign = ContentAlignment.MiddleLeft
            };

            listValues = new ListView()
            {
                Dock = DockStyle.Fill,
                View = View.Details,
                FullRowSelect = true,
                GridLines = true,
                BorderStyle = BorderStyle.FixedSingle,
                MultiSelect = false,
                Font = new Font("Segoe UI", 9F, FontStyle.Regular)
            };
            listValues.Columns.Add("Nome Valore", 180);
            listValues.Columns.Add("Tipo", 95);
            listValues.Columns.Add("Dati / Valore Assegnato", 220);
            listValues.Columns.Add("Stato Update", 95);
            listValues.Columns.Add("Safety Check (Anti-Instabilita')", 210);

            listValues.DoubleClick += (s, e) => OnEditSelectedValue();
            listValues.Resize += (s, e) => AdjustListViewColumns();

            pnlRight.Controls.Add(listValues);
            pnlRight.Controls.Add(lblListTitle);
            lblListTitle.BringToFront();
            listValues.BringToFront();
            split.Panel2.Controls.Add(pnlRight);
        }

        private void AdjustListViewColumns()
        {
            if (listValues == null || listValues.Columns.Count < 5) return;
            int avail = listValues.ClientSize.Width - SystemInformation.VerticalScrollBarWidth - 4;
            if (avail < 550) return;

            int colName = 180;
            int colType = 95;
            int colStatus = 95;
            int colSafety = 210;
            int colData = Math.Max(160, avail - (colName + colType + colStatus + colSafety));

            listValues.Columns[0].Width = colName;
            listValues.Columns[1].Width = colType;
            listValues.Columns[2].Width = colData;
            listValues.Columns[3].Width = colStatus;
            listValues.Columns[4].Width = colSafety;
        }

        private void BuildStatusBar()
        {
            statusBar = new StatusStrip();
            statusBar.Dock = DockStyle.Bottom;
            lblStatusFile = new ToolStripStatusLabel("Nessun database caricato") { AutoSize = true };
            lblStatusCrypto = new ToolStripStatusLabel("AES-256: Inattivo") { BorderSides = ToolStripStatusLabelBorderSides.Left };
            lblStatusModified = new ToolStripStatusLabel("Modifiche Update: 0") { BorderSides = ToolStripStatusLabelBorderSides.Left, Font = new Font("Segoe UI", 8.5F, FontStyle.Bold) };

            statusBar.Items.AddRange(new ToolStripItem[] { lblStatusFile, lblStatusCrypto, lblStatusModified });
        }

        private void OnOpenFile()
        {
            using (OpenFileDialog ofd = new OpenFileDialog())
            {
                ofd.Title = "Seleziona File di Registro NewOS (REGISTRY.DAT)";
                ofd.Filter = "File di Registro (*.dat;*.ini)|*.dat;*.ini|Tutti i file (*.*)|*.*";
                if (ofd.ShowDialog() == DialogResult.OK)
                {
                    LoadRegistryFile(ofd.FileName);
                }
            }
        }

        private void LoadRegistryFile(string path)
        {
            try
            {
                db.LoadFromFile(path);
                PopulateTree();
                lblStatusFile.Text = "Database: " + Path.GetFileName(path) + " (" + db.TotalKeysCount + " chiavi, " + db.TotalValuesCount + " valori)";
                lblStatusCrypto.Text = db.IsEncryptedSource ? "AES-256: Protetto (Decifrato)" : "Stato: In Chiaro";
                lblStatusCrypto.ForeColor = db.IsEncryptedSource ? Color.DarkGreen : Color.DarkOrange;
                UpdateModifiedStatus();
            }
            catch (Exception ex)
            {
                MessageBox.Show("Errore durante il caricamento di REGISTRY.DAT:\n" + ex.Message, "Errore Database", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }

        private void PopulateTree()
        {
            treeRegistry.BeginUpdate();
            treeRegistry.Nodes.Clear();

            TreeNode firstNodeWithValues = null;

            foreach (var kvp in db.RootKeys)
            {
                TreeNode rootNode = new TreeNode(kvp.Key);
                rootNode.Tag = kvp.Value;
                AddSubTreeNodes(rootNode, kvp.Value, ref firstNodeWithValues);
                treeRegistry.Nodes.Add(rootNode);
                rootNode.Expand();
            }

            treeRegistry.EndUpdate();

            if (firstNodeWithValues != null)
            {
                treeRegistry.SelectedNode = firstNodeWithValues;
            }
            else if (treeRegistry.Nodes.Count > 0)
            {
                treeRegistry.SelectedNode = treeRegistry.Nodes[0];
            }
        }

        private void AddSubTreeNodes(TreeNode parentNode, RegistryNodeItem nodeItem, ref TreeNode firstNodeWithValues)
        {
            foreach (var kvp in nodeItem.SubKeys)
            {
                TreeNode childNode = new TreeNode(kvp.Key);
                childNode.Tag = kvp.Value;
                if (kvp.Value.IsNewKey)
                {
                    childNode.Text += " [NUOVA]";
                    childNode.ForeColor = Color.Blue;
                }
                if (firstNodeWithValues == null && kvp.Value.Values.Count > 0)
                {
                    firstNodeWithValues = childNode;
                }
                AddSubTreeNodes(childNode, kvp.Value, ref firstNodeWithValues);
                parentNode.Nodes.Add(childNode);
            }
        }

        private void DisplayValuesForNode(RegistryNodeItem node)
        {
            listValues.BeginUpdate();
            listValues.Items.Clear();

            if (node != null)
            {
                foreach (var val in node.Values.Values)
                {
                    ListViewItem item = new ListViewItem(val.Name);
                    item.SubItems.Add(val.Type.ToString());
                    item.SubItems.Add(val.Data);
                    item.SubItems.Add(val.Action.ToString());

                    string safetyText = val.Stability == StabilityLevel.STABLE ? "STABILE (Consentito)" : "INSTABILE / PROIBITO";
                    item.SubItems.Add(safetyText);

                    if (val.Stability == StabilityLevel.UNSTABLE_PROHIBITED)
                    {
                        item.ForeColor = Color.Crimson;
                        item.Font = new Font(listValues.Font, FontStyle.Strikeout);
                    }
                    else if (val.Action == UpdateItemAction.SET_VALUE || val.Action == UpdateItemAction.CREATE_KEY)
                    {
                        item.ForeColor = Color.DarkBlue;
                        item.Font = new Font(listValues.Font, FontStyle.Bold);
                    }

                    item.Tag = val;
                    listValues.Items.Add(item);
                }
            }

            listValues.EndUpdate();
            AdjustListViewColumns();
        }

        private void OnNewKey()
        {
            if (treeRegistry.SelectedNode == null)
            {
                MessageBox.Show("Seleziona prima una chiave genitore nell'albero gerarchico.", "Attenzione", MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }

            RegistryNodeItem selectedNode = treeRegistry.SelectedNode.Tag as RegistryNodeItem;
            string input = ShowInputDialog("Inserisci il nome della nuova chiave / sottochiave:", "Nuova Chiave");
            if (!string.IsNullOrWhiteSpace(input))
            {
                string newPath = selectedNode.FullPath + "\\" + input.Trim();
                RegistryNodeItem newNode = db.FindOrCreateNode(newPath, true);

                TreeNode newTreeNode = new TreeNode(input.Trim() + " [NUOVA]")
                {
                    Tag = newNode,
                    ForeColor = Color.Blue
                };
                treeRegistry.SelectedNode.Nodes.Add(newTreeNode);
                treeRegistry.SelectedNode.Expand();
                treeRegistry.SelectedNode = newTreeNode;

                if (!newKeyPathsList.Contains(newPath))
                    newKeyPathsList.Add(newPath);

                UpdateModifiedStatus();
            }
        }

        private void OnNewValue()
        {
            if (treeRegistry.SelectedNode == null)
            {
                MessageBox.Show("Seleziona prima una chiave di destinazione nell'albero.", "Attenzione", MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }

            RegistryNodeItem selectedNode = treeRegistry.SelectedNode.Tag as RegistryNodeItem;
            using (EditValueForm dlg = new EditValueForm(selectedNode.FullPath, "", RegValueType.REG_SZ, "", false))
            {
                if (dlg.ShowDialog(this) == DialogResult.OK)
                {
                    var valItem = new RegistryValueItem(selectedNode.FullPath, dlg.ValueName, dlg.ValueType, dlg.ValueData, false);
                    valItem.Action = UpdateItemAction.SET_VALUE;
                    selectedNode.Values[dlg.ValueName] = valItem;

                    modifiedValuesList.RemoveAll(x => x.KeyPath.Equals(selectedNode.FullPath, StringComparison.OrdinalIgnoreCase) && x.Name.Equals(dlg.ValueName, StringComparison.OrdinalIgnoreCase));
                    modifiedValuesList.Add(valItem);

                    DisplayValuesForNode(selectedNode);
                    UpdateModifiedStatus();
                }
            }
        }

        private void OnEditSelectedValue()
        {
            if (listValues.SelectedItems.Count == 0) return;
            RegistryValueItem val = listValues.SelectedItems[0].Tag as RegistryValueItem;
            if (val == null) return;

            using (EditValueForm dlg = new EditValueForm(val.KeyPath, val.Name, val.Type, val.Data, true))
            {
                if (dlg.ShowDialog(this) == DialogResult.OK)
                {
                    val.Type = dlg.ValueType;
                    val.Data = dlg.ValueData;
                    val.Action = UpdateItemAction.SET_VALUE;
                    val.Stability = SafetyChecker.Evaluate(val.KeyPath, val.Name);

                    if (!modifiedValuesList.Contains(val))
                        modifiedValuesList.Add(val);

                    DisplayValuesForNode(treeRegistry.SelectedNode.Tag as RegistryNodeItem);
                    UpdateModifiedStatus();
                }
            }
        }

        private void OnResetModifications()
        {
            if (MessageBox.Show("Vuoi azzerare tutte le chiavi e i valori registrati in modalita' Update?", "Conferma Reset", MessageBoxButtons.YesNo, MessageBoxIcon.Question) == DialogResult.Yes)
            {
                modifiedValuesList.Clear();
                newKeyPathsList.Clear();
                if (!string.IsNullOrEmpty(db.SourcePath))
                    LoadRegistryFile(db.SourcePath);
                UpdateModifiedStatus();
            }
        }

        private void UpdateModifiedStatus()
        {
            lblStatusModified.Text = string.Format("Modifiche Update: {0} valori, {1} nuove chiavi", modifiedValuesList.Count, newKeyPathsList.Count);
            lblStatusModified.ForeColor = (modifiedValuesList.Count > 0 || newKeyPathsList.Count > 0) ? Color.FromArgb(12, 108, 230) : Color.Black;
        }

        private void OnExportUpdate()
        {
            if (modifiedValuesList.Count == 0 && newKeyPathsList.Count == 0)
            {
                MessageBox.Show("Nessuna modifica o nuova chiave presente da esportare.\nAggiungi prima una chiave o modifica un valore di registro in modalita' Update.", "Nessuna Modifica", MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }

            // Verifica presenza di chiavi instabili
            List<RegistryValueItem> unstableItems = modifiedValuesList.FindAll(x => x.Stability == StabilityLevel.UNSTABLE_PROHIBITED);
            if (unstableItems.Count > 0)
            {
                string msg = string.Format("ATTENZIONE SAFETY CHECK:\nSono presenti {0} elementi classificati come INSTABILI / PROIBITI:\n", unstableItems.Count);
                foreach (var it in unstableItems)
                {
                    msg += string.Format("- {0}\\{1}\n", it.KeyPath, it.Name);
                }
                msg += "\nUPDATE.MOD rifiutera' la scrittura di questi parametri.\nVuoi escludere automaticamente le chiavi instabili ed esportare solo quelle stabili autorizzate?";

                var res = MessageBox.Show(msg, "Avviso di Sicurezza", MessageBoxButtons.YesNoCancel, MessageBoxIcon.Warning);
                if (res == DialogResult.Yes)
                {
                    modifiedValuesList.RemoveAll(x => x.Stability == StabilityLevel.UNSTABLE_PROHIBITED);
                }
                else if (res == DialogResult.Cancel)
                {
                    return;
                }
            }

            using (SaveFileDialog sfd = new SaveFileDialog())
            {
                sfd.Title = "Salva Pacchetto Differenziale e Certificato NewSoft";
                sfd.FileName = "registry_update.update";
                sfd.Filter = "File Update NewOS (*.update)|*.update|Tutti i file (*.*)|*.*";

                if (sfd.ShowDialog() == DialogResult.OK)
                {
                    try
                    {
                        string outputContent = UpdatePackageGenerator.GenerateUpdateScript(modifiedValuesList, newKeyPathsList);
                        File.WriteAllText(sfd.FileName, outputContent, Encoding.UTF8);

                        string msg = string.Format("Generazione e firma digitale completate con successo!\n\nFile: {0}\nChiavi create: {1}\nValori aggiornati: {2}\nCertificato NewSoft: REG-AUTH integrato con HMAC-SHA256.\n\nIl file e' pronto per essere inserito nel pacchetto UPDATE.PAK.",
                            Path.GetFileName(sfd.FileName), newKeyPathsList.Count, modifiedValuesList.Count);

                        MessageBox.Show(msg, "Esportazione Riuscita", MessageBoxButtons.OK, MessageBoxIcon.Information);
                    }
                    catch (Exception ex)
                    {
                        MessageBox.Show("Errore durante l'esportazione di registry_update.update:\n" + ex.Message, "Errore", MessageBoxButtons.OK, MessageBoxIcon.Error);
                    }
                }
            }
        }

        private void ShowAbout()
        {
            string info = "NewOS V1 - Registry Update Editor\n" +
                          "Suite di Manutenzione e Firma per Windows\n\n" +
                          "Posizione di Distribuzione: registry_update_editor\\\n" +
                          "Supporto: Decifratura AES-256 CBC di REGISTRY.DAT\n" +
                          "Safety Check: Anti-Instabilita' su risorse hardware BIOS/PCI/DMA e profili\n" +
                          "Esportazione: registry_update.update con certificato HMAC-SHA256\n\n" +
                          "(C) 2026 Newspoint Software - NewSoft Root Authority";
            MessageBox.Show(info, "Informazioni su Registry Update Editor", MessageBoxButtons.OK, MessageBoxIcon.Information);
        }

        private static string ShowInputDialog(string prompt, string title)
        {
            Form form = new Form()
            {
                Width = 420,
                Height = 160,
                FormBorderStyle = FormBorderStyle.FixedDialog,
                Text = title,
                StartPosition = FormStartPosition.CenterParent,
                MaximizeBox = false,
                MinimizeBox = false,
                Font = new Font("Segoe UI", 9F)
            };
            Label textLabel = new Label() { Left = 20, Top = 15, Text = prompt, AutoSize = true };
            TextBox textBox = new TextBox() { Left = 20, Top = 40, Width = 360 };
            Button confirmation = new Button() { Text = "OK", Left = 200, Width = 85, Top = 75, DialogResult = DialogResult.OK };
            Button cancel = new Button() { Text = "Annulla", Left = 295, Width = 85, Top = 75, DialogResult = DialogResult.Cancel };
            form.Controls.Add(textLabel);
            form.Controls.Add(textBox);
            form.Controls.Add(confirmation);
            form.Controls.Add(cancel);
            form.AcceptButton = confirmation;
            form.CancelButton = cancel;
            return form.ShowDialog() == DialogResult.OK ? textBox.Text : "";
        }

        [STAThread]
        public static void Main(string[] args)
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new MainForm());
        }
    }
}
