using System.Diagnostics;
using WorldClientCore.Authentication;
using WorldClientCore.Models;
using WorldClientCore.Networking;
using WorldClientCore.Simulation;
using WorldClientWinForms.Configuration;
using WorldClientWinForms.Controls;

namespace WorldClientWinForms;

internal sealed class MainForm : Form, IMessageFilter
{
    private const int WmKeyDown = 0x0100;
    private const int WmKeyUp = 0x0101;
    private const int WmSysKeyDown = 0x0104;
    private const int WmSysKeyUp = 0x0105;

    private static readonly TimeSpan s_moveSyncInterval = TimeSpan.FromMilliseconds(200.0);
    private static readonly Color s_background = Color.FromArgb(15, 20, 26);
    private static readonly Color s_panel = Color.FromArgb(24, 31, 39);
    private static readonly Color s_panelLight = Color.FromArgb(33, 42, 52);
    private static readonly Color s_gold = Color.FromArgb(198, 155, 75);
    private static readonly Color s_text = Color.FromArgb(232, 235, 238);
    private static readonly Color s_muted = Color.FromArgb(160, 169, 178);

    private readonly ClientSettings m_settings;
    private readonly string m_clientName;
    private readonly WorldTcpClient m_client = new();
    private readonly WorldLoginApiClient m_loginApiClient = new();
    private readonly Panel m_loginPanel = new() { Dock = DockStyle.Fill, AutoScroll = true };
    private readonly Panel m_mainPanel = new() { Dock = DockStyle.Fill, Visible = false };
    private readonly TabControl m_authTabControl = new() { Dock = DockStyle.Fill };
    private readonly SplitContainer m_contentSplitContainer = new()
    {
        Dock = DockStyle.Fill,
        Orientation = Orientation.Vertical,
        FixedPanel = FixedPanel.Panel2
    };
    private readonly Label m_loginServerLabel = new() { AutoSize = true };
    private readonly Label m_loginStatusLabel = new() { AutoSize = true, Text = "● 로그인 또는 회원가입을 선택하세요" };
    private readonly Label m_userLabel = CreateValueLabel("-");
    private readonly WorldViewControl m_worldView = new() { Dock = DockStyle.Fill };
    private readonly Label m_connectionLabel = CreateValueLabel("Disconnected");
    private readonly Label m_authLabel = CreateValueLabel("-");
    private readonly Label m_mapLabel = CreateValueLabel("-");
    private readonly Label m_entityLabel = CreateValueLabel("-");
    private readonly Label m_positionLabel = CreateValueLabel("-");
    private readonly Label m_actorCountLabel = CreateValueLabel("0");
    private readonly Label m_targetLabel = CreateValueLabel("-");
    private readonly Label m_hpLabel = CreateValueLabel("-");
    private readonly Label m_lifeStateLabel = CreateValueLabel("-");
    private readonly Label m_mpLabel = CreateValueLabel("-");
    private readonly Label m_finalStrDexLabel = CreateValueLabel("-");
    private readonly Label m_finalIntLukLabel = CreateValueLabel("-");
    private readonly Label m_attackDefenseLabel = CreateValueLabel("-");
    private readonly Label m_moveSpeedLabel = CreateValueLabel("-");
    private readonly Label m_equipmentVersionLabel = CreateValueLabel("-");
    private readonly Label m_statRevisionLabel = CreateValueLabel("-");
    private readonly NumericUpDown m_mapDataIdInput = new() { Minimum = 1, Maximum = uint.MaxValue, Dock = DockStyle.Fill };
    private readonly NumericUpDown m_itemInstanceIdInput = new()
    {
        Minimum = 1,
        Maximum = (decimal)ulong.MaxValue,
        Value = 1,
        ThousandsSeparator = true,
        Dock = DockStyle.Fill
    };
    private readonly NumericUpDown m_expectedItemVersionInput = new()
    {
        Minimum = 1,
        Maximum = (decimal)ulong.MaxValue,
        Value = 1,
        ThousandsSeparator = true,
        Dock = DockStyle.Fill
    };
    private readonly TextBox m_loginIdTextBox = new() { Width = 115 };
    private readonly TextBox m_passwordTextBox = new() { Width = 115, UseSystemPasswordChar = true };
    private readonly TextBox m_registerIdTextBox = new() { Dock = DockStyle.Fill };
    private readonly TextBox m_registerPasswordTextBox = new() { Dock = DockStyle.Fill, UseSystemPasswordChar = true };
    private readonly TextBox m_registerNicknameTextBox = new() { Dock = DockStyle.Fill };
    private readonly Button m_connectButton = new() { Text = "로그인" };
    private readonly Button m_registerButton = new() { Text = "회원가입" };
    private readonly Button m_directConnectButton = new() { Text = "개발용 직접 접속" };
    private readonly Button m_disconnectButton = new() { Text = "Disconnect", AutoSize = true };
    private readonly Button m_enterMapButton = new() { Text = "Enter Map", AutoSize = true };
    private readonly Button m_equipButton = new() { Text = "Equip", AutoSize = true };
    private readonly Button m_unequipButton = new() { Text = "Unequip", AutoSize = true };
    private readonly RichTextBox m_logTextBox = new()
    {
        Dock = DockStyle.Fill,
        ReadOnly = true,
        BackColor = Color.FromArgb(19, 24, 29),
        ForeColor = Color.FromArgb(210, 220, 225),
        BorderStyle = BorderStyle.FixedSingle,
        Font = new Font("Consolas", 9.0f)
    };
    private readonly System.Windows.Forms.Timer m_renderTimer = new() { Interval = 16 };
    private readonly Stopwatch m_frameClock = Stopwatch.StartNew();
    private readonly HashSet<Keys> m_pressedMovementKeys = [];

    private bool m_isConnected;
    private bool m_isAuthenticated;
    private bool m_isCompatibilityMode;
    private bool m_mapEnterPending;
    private bool m_equipmentRequestPending;
    private bool m_hasPlayerStats;
    private bool m_localMoving;
    private bool m_closing;
    private bool m_authOperationPending;
    private bool m_loginFlowPending;
    private bool m_preserveLoginStatusOnDisconnect;
    private bool m_attackKeyPressed;
    private ulong m_nextRequestId = 1;
    private uint m_moveSequence;
    private uint m_attackSequence;
    private float m_currentDirectionX;
    private float m_currentDirectionY = 1.0f;
    private TimeSpan m_lastMoveSyncTime;

    public MainForm(ClientSettings settings, string clientName)
    {
        m_settings = settings;
        m_clientName = clientName;
        Text = $"World Client · {clientName}";
        StartPosition = FormStartPosition.CenterScreen;
        AutoScaleMode = AutoScaleMode.Dpi;
        MinimumSize = new Size(1080, 720);
        ClientSize = new Size(1220, 800);
        KeyPreview = true;
        BackColor = s_background;
        ForeColor = s_text;
        Font = new Font("Segoe UI", 9.0f);

        m_mapDataIdInput.Value = m_settings.DefaultMapDataId;
        m_loginIdTextBox.Text = m_settings.LoginId;
        m_passwordTextBox.Text = m_settings.Password;
        BuildLoginView();
        InitializeUi();
        Controls.Add(m_mainPanel);
        Controls.Add(m_loginPanel);
        WireEvents();
        UpdateUiState();
        Application.AddMessageFilter(this);
        m_renderTimer.Start();
    }

    internal void ShowMainView()
    {
        m_loginPanel.Visible = false;
        m_mainPanel.Visible = true;
        m_mainPanel.BringToFront();
    }

    internal bool HasReadableStatusPanel =>
        m_contentSplitContainer.Panel2.ClientSize.Width >= LogicalToDeviceUnits(330);

    public bool PreFilterMessage(ref Message message)
    {
        if (m_closing || Form.ActiveForm != this || !m_mainPanel.Visible || m_loginPanel.Visible)
        {
            return false;
        }

        bool isKeyDown = message.Msg is WmKeyDown or WmSysKeyDown;
        bool isKeyUp = message.Msg is WmKeyUp or WmSysKeyUp;
        if (isKeyDown || isKeyUp)
        {
            Keys keyCode = unchecked((Keys)message.WParam.ToInt64()) & Keys.KeyCode;
            if (IsMovementKey(keyCode))
            {
                bool stateChanged = isKeyDown
                    ? m_pressedMovementKeys.Add(keyCode)
                    : m_pressedMovementKeys.Remove(keyCode);
                if (stateChanged)
                {
                    UpdateMovementFromKeys();
                }
                return true;
            }
            if (keyCode == Keys.Space)
            {
                if (isKeyDown)
                {
                    if (!m_attackKeyPressed)
                    {
                        m_attackKeyPressed = true;
                        SendBasicAttack();
                    }
                }
                else
                {
                    m_attackKeyPressed = false;
                }
                return true;
            }
        }
        return false;
    }

    protected override void OnDeactivate(EventArgs e)
    {
        m_attackKeyPressed = false;
        StopMovement();
        base.OnDeactivate(e);
    }

    protected override void OnFormClosing(FormClosingEventArgs e)
    {
        if (!m_closing)
        {
            m_closing = true;
            m_renderTimer.Stop();
            StopMovement();
            try
            {
                m_client.DisposeAsync().AsTask().GetAwaiter().GetResult();
            }
            catch
            {
            }
        }
        base.OnFormClosing(e);
    }

    protected override void OnFormClosed(FormClosedEventArgs e)
    {
        Application.RemoveMessageFilter(this);
        base.OnFormClosed(e);
    }

    private void BuildLoginView()
    {
        TableLayoutPanel root = new()
        {
            Dock = DockStyle.Fill,
            ColumnCount = 1,
            RowCount = 1,
            AutoScroll = true,
            BackColor = s_background,
            Padding = new Padding(16)
        };
        root.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100.0f));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 100.0f));

        Panel card = new()
        {
            Size = new Size(540, 610),
            Padding = new Padding(38, 24, 38, 24),
            BackColor = s_panel,
            Anchor = AnchorStyles.None
        };
        TableLayoutPanel fields = new()
        {
            Dock = DockStyle.Fill,
            ColumnCount = 1,
            RowCount = 5
        };
        fields.RowStyles.Add(new RowStyle(SizeType.Absolute, 64.0f));
        fields.RowStyles.Add(new RowStyle(SizeType.Absolute, 30.0f));
        fields.RowStyles.Add(new RowStyle(SizeType.Percent, 100.0f));
        fields.RowStyles.Add(new RowStyle(SizeType.Absolute, 48.0f));
        fields.RowStyles.Add(new RowStyle(SizeType.Absolute, 40.0f));
        fields.Controls.Add(new Label
        {
            Text = "WORLD SERVER",
            Dock = DockStyle.Fill,
            Font = new Font("Segoe UI", 22.0f, FontStyle.Bold),
            ForeColor = s_gold,
            TextAlign = ContentAlignment.MiddleCenter
        }, 0, 0);

        m_loginServerLabel.Text = $"LoginServer  {m_settings.LoginServerBaseUrl}";
        m_loginServerLabel.ForeColor = s_muted;
        m_loginServerLabel.Anchor = AnchorStyles.None;
        fields.Controls.Add(m_loginServerLabel, 0, 1);

        m_authTabControl.TabPages.Add(BuildLoginTab());
        m_authTabControl.TabPages.Add(BuildRegisterTab());
        fields.Controls.Add(m_authTabControl, 0, 2);

        m_loginStatusLabel.ForeColor = s_muted;
        m_loginStatusLabel.Dock = DockStyle.Fill;
        m_loginStatusLabel.AutoSize = false;
        m_loginStatusLabel.TextAlign = ContentAlignment.MiddleLeft;
        m_loginStatusLabel.AutoEllipsis = true;
        fields.Controls.Add(m_loginStatusLabel, 0, 3);
        fields.Controls.Add(new Label
        {
            Text = "인증 성공 후 LoginServer가 알려준 WorldServer로 연결합니다.",
            Dock = DockStyle.Fill,
            ForeColor = s_muted,
            TextAlign = ContentAlignment.MiddleLeft
        }, 0, 4);

        card.Controls.Add(fields);
        root.Controls.Add(card, 0, 0);
        m_loginPanel.Controls.Add(root);
    }

    private TabPage BuildLoginTab()
    {
        TabPage page = new("로그인") { BackColor = s_panel, Padding = new Padding(22, 18, 22, 14) };
        TableLayoutPanel fields = CreateAuthFields(8);
        fields.RowStyles.Add(new RowStyle(SizeType.Absolute, 28.0f));
        fields.RowStyles.Add(new RowStyle(SizeType.Absolute, 46.0f));
        fields.RowStyles.Add(new RowStyle(SizeType.Absolute, 28.0f));
        fields.RowStyles.Add(new RowStyle(SizeType.Absolute, 46.0f));
        fields.RowStyles.Add(new RowStyle(SizeType.Percent, 100.0f));
        fields.RowStyles.Add(new RowStyle(SizeType.Absolute, 48.0f));
        fields.RowStyles.Add(new RowStyle(SizeType.Absolute, 8.0f));
        fields.RowStyles.Add(new RowStyle(SizeType.Absolute, 44.0f));

        m_loginIdTextBox.Dock = DockStyle.Fill;
        m_passwordTextBox.Dock = DockStyle.Fill;
        fields.Controls.Add(CreateAuthLabel("아이디"));
        fields.Controls.Add(m_loginIdTextBox);
        fields.Controls.Add(CreateAuthLabel("비밀번호"));
        fields.Controls.Add(m_passwordTextBox);
        fields.Controls.Add(new Panel());
        ConfigurePrimaryButton(m_connectButton);
        m_connectButton.Dock = DockStyle.Fill;
        fields.Controls.Add(m_connectButton);
        fields.Controls.Add(new Panel());
        ConfigureSecondaryButton(m_directConnectButton);
        m_directConnectButton.Dock = DockStyle.Fill;
        fields.Controls.Add(m_directConnectButton);
        page.Controls.Add(fields);
        return page;
    }

    private TabPage BuildRegisterTab()
    {
        TabPage page = new("회원가입") { BackColor = s_panel, Padding = new Padding(22, 14, 22, 14) };
        TableLayoutPanel fields = CreateAuthFields(8);
        for (int index = 0; index < 3; ++index)
        {
            fields.RowStyles.Add(new RowStyle(SizeType.Absolute, 25.0f));
            fields.RowStyles.Add(new RowStyle(SizeType.Absolute, 42.0f));
        }
        fields.RowStyles.Add(new RowStyle(SizeType.Percent, 100.0f));
        fields.RowStyles.Add(new RowStyle(SizeType.Absolute, 48.0f));
        fields.Controls.Add(CreateAuthLabel("아이디"));
        fields.Controls.Add(m_registerIdTextBox);
        fields.Controls.Add(CreateAuthLabel("비밀번호"));
        fields.Controls.Add(m_registerPasswordTextBox);
        fields.Controls.Add(CreateAuthLabel("닉네임"));
        fields.Controls.Add(m_registerNicknameTextBox);
        fields.Controls.Add(new Panel());
        ConfigurePrimaryButton(m_registerButton);
        m_registerButton.Dock = DockStyle.Fill;
        fields.Controls.Add(m_registerButton);
        page.Controls.Add(fields);
        return page;
    }

    private static TableLayoutPanel CreateAuthFields(int rowCount)
    {
        TableLayoutPanel fields = new() { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = rowCount };
        fields.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100.0f));
        return fields;
    }

    private static Label CreateAuthLabel(string text) => new()
    {
        Text = text,
        Dock = DockStyle.Fill,
        ForeColor = s_text,
        TextAlign = ContentAlignment.BottomLeft
    };

    private static void ConfigurePrimaryButton(Button button)
    {
        button.FlatStyle = FlatStyle.Flat;
        button.FlatAppearance.BorderColor = s_gold;
        button.BackColor = Color.FromArgb(76, 58, 32);
        button.ForeColor = s_text;
    }

    private static void ConfigureSecondaryButton(Button button)
    {
        button.FlatStyle = FlatStyle.Flat;
        button.FlatAppearance.BorderColor = Color.FromArgb(71, 79, 88);
        button.BackColor = s_panelLight;
        button.ForeColor = s_muted;
    }

    private void InitializeUi()
    {
        TableLayoutPanel root = new()
        {
            Dock = DockStyle.Fill,
            ColumnCount = 1,
            RowCount = 3,
            Padding = new Padding(10)
        };
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 72.0f));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 28.0f));

        FlowLayoutPanel toolbar = new()
        {
            Dock = DockStyle.Fill,
            AutoSize = true,
            WrapContents = true,
            Padding = new Padding(0, 0, 0, 7)
        };
        toolbar.Controls.Add(CreateCaption("User"));
        toolbar.Controls.Add(m_userLabel);
        toolbar.Controls.Add(m_disconnectButton);
        toolbar.Controls.Add(CreateCaption("MapDataId"));
        m_mapDataIdInput.Width = 90;
        toolbar.Controls.Add(m_mapDataIdInput);
        toolbar.Controls.Add(m_enterMapButton);
        toolbar.Controls.Add(CreateCaption("  WASD / Arrow keys: Move · Click Monster + Space: Attack"));

        m_contentSplitContainer.Panel1.Controls.Add(m_worldView);
        m_contentSplitContainer.Panel2.Controls.Add(CreateStatusPanel());

        root.Controls.Add(toolbar, 0, 0);
        root.Controls.Add(m_contentSplitContainer, 0, 1);
        root.Controls.Add(m_logTextBox, 0, 2);
        m_mainPanel.Controls.Add(root);

        m_mainPanel.VisibleChanged += (_, _) => ScheduleContentLayout();
    }

    private void ApplyInitialSplitterDistance(SplitContainer splitContainer)
    {
        int availableWidth = splitContainer.ClientSize.Width - splitContainer.SplitterWidth;
        int panel1MinSize = LogicalToDeviceUnits(520);
        int panel2MinSize = LogicalToDeviceUnits(330);
        if (availableWidth <= panel1MinSize + panel2MinSize)
        {
            return;
        }

        int desiredPanel2Width = LogicalToDeviceUnits(350);
        int minimumDistance = panel1MinSize;
        int maximumDistance = availableWidth - panel2MinSize;
        int splitterDistance = Math.Clamp(
            availableWidth - desiredPanel2Width,
            minimumDistance,
            maximumDistance);

        // The main panel is hidden during login, so the SplitContainer can still
        // hold its narrow design-time distance when this is first applied.
        splitContainer.Panel1MinSize = 0;
        splitContainer.Panel2MinSize = 0;
        splitContainer.SplitterDistance = splitterDistance;
        splitContainer.Panel1MinSize = panel1MinSize;
        splitContainer.Panel2MinSize = panel2MinSize;
    }

    private void ScheduleContentLayout()
    {
        if (!m_mainPanel.Visible || !IsHandleCreated || m_closing)
        {
            return;
        }

        BeginInvoke(new Action(() =>
        {
            if (!m_closing && m_mainPanel.Visible && !m_contentSplitContainer.IsDisposed)
            {
                ApplyInitialSplitterDistance(m_contentSplitContainer);
            }
        }));
    }

    private Control CreateStatusPanel()
    {
        Panel container = new()
        {
            Dock = DockStyle.Fill,
            AutoScroll = true,
            BackColor = Color.FromArgb(35, 44, 52)
        };
        TableLayoutPanel panel = new()
        {
            Dock = DockStyle.Top,
            AutoSize = true,
            AutoSizeMode = AutoSizeMode.GrowAndShrink,
            ColumnCount = 2,
            RowCount = 0,
            Padding = new Padding(12),
            BackColor = Color.FromArgb(35, 44, 52)
        };
        panel.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        panel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100.0f));
        int row = 0;
        AddSectionHeader(panel, row++, "Session");
        AddStatusRow(panel, row++, "Client", CreateValueLabel(m_clientName));
        AddStatusRow(panel, row++, "Connection", m_connectionLabel);
        AddStatusRow(panel, row++, "Authentication", m_authLabel);
        AddStatusRow(panel, row++, "Map Instance", m_mapLabel);
        AddStatusRow(panel, row++, "Entity", m_entityLabel);
        AddStatusRow(panel, row++, "Position", m_positionLabel);
        AddStatusRow(panel, row++, "Actors", m_actorCountLabel);
        AddStatusRow(panel, row++, "Target", m_targetLabel);

        AddSectionHeader(panel, row++, "Player Combat Stats");
        AddStatusRow(panel, row++, "HP", m_hpLabel);
        AddStatusRow(panel, row++, "Life", m_lifeStateLabel);
        AddStatusRow(panel, row++, "MP", m_mpLabel);
        AddStatusRow(panel, row++, "Final STR / DEX", m_finalStrDexLabel);
        AddStatusRow(panel, row++, "Final INT / LUK", m_finalIntLukLabel);
        AddStatusRow(panel, row++, "Attack / Defense", m_attackDefenseLabel);
        AddStatusRow(panel, row++, "Move Speed", m_moveSpeedLabel);
        AddStatusRow(panel, row++, "Equipment Ver.", m_equipmentVersionLabel);
        AddStatusRow(panel, row++, "Stat Revision", m_statRevisionLabel);

        AddSectionHeader(panel, row++, "Equipment Command");
        AddStatusRow(panel, row++, "Item Instance ID", m_itemInstanceIdInput);
        AddStatusRow(panel, row++, "Expected Version", m_expectedItemVersionInput);
        FlowLayoutPanel actionButtons = new()
        {
            AutoSize = true,
            WrapContents = false,
            Margin = new Padding(4, 8, 0, 4)
        };
        actionButtons.Controls.Add(m_equipButton);
        actionButtons.Controls.Add(m_unequipButton);
        panel.Controls.Add(actionButtons, 0, row);
        panel.SetColumnSpan(actionButtons, 2);
        ++row;

        Label hint = new()
        {
            AutoSize = true,
            MaximumSize = new Size(300, 0),
            Text = "Use the item version returned by the inventory or the previous equipment response.",
            ForeColor = Color.FromArgb(160, 177, 188),
            Padding = new Padding(0, 8, 0, 4)
        };
        panel.Controls.Add(hint, 0, row);
        panel.SetColumnSpan(hint, 2);
        container.Controls.Add(panel);
        return container;
    }

    private void WireEvents()
    {
        m_connectButton.Click += async (_, _) => await ConnectAsync();
        m_directConnectButton.Click += async (_, _) => await DirectConnectAsync();
        m_registerButton.Click += async (_, _) => await RegisterAsync();
        m_disconnectButton.Click += async (_, _) => await DisconnectAsync();
        m_enterMapButton.Click += async (_, _) => await EnterMapAsync();
        m_equipButton.Click += async (_, _) => await ExecuteEquipmentMutationAsync(equip: true);
        m_unequipButton.Click += async (_, _) => await ExecuteEquipmentMutationAsync(equip: false);
        m_renderTimer.Tick += (_, _) => RenderTick();

        m_passwordTextBox.KeyDown += async (_, eventArgs) =>
        {
            if (eventArgs.KeyCode == Keys.Enter)
            {
                eventArgs.SuppressKeyPress = true;
                await ConnectAsync();
            }
        };
        m_registerNicknameTextBox.KeyDown += async (_, eventArgs) =>
        {
            if (eventArgs.KeyCode == Keys.Enter)
            {
                eventArgs.SuppressKeyPress = true;
                await RegisterAsync();
            }
        };

        m_client.SystemMessageReceived += message => RunOnUiThread(() =>
        {
            AppendLog(message);
            if (m_loginPanel.Visible && !m_preserveLoginStatusOnDisconnect)
            {
                SetLoginStatus(message, isError: false);
            }
        });
        m_client.ConnectionStateChanged += connected => RunOnUiThread(() => HandleConnectionChanged(connected));
        m_client.MapEnterResultReceived += result => RunOnUiThread(() => HandleMapEnterResult(result));
        m_client.EquipmentMutationResultReceived += result => RunOnUiThread(() => HandleEquipmentMutationResult(result));
        m_client.ActorSpawnReceived += notification => RunOnUiThread(() =>
        {
            m_worldView.Spawn(notification);
            AppendLog(
                $"Spawn kind={notification.ActorKind}, data={notification.ActorDataId}, " +
                $"entity={notification.EntityId}, pos=({notification.PositionX:F1}, {notification.PositionY:F1})");
            UpdateStatus();
        });
        m_client.ActorDespawnReceived += notification => RunOnUiThread(() =>
        {
            m_worldView.Despawn(notification.EntityId);
            AppendLog($"Despawn entity={notification.EntityId}");
            UpdateStatus();
        });
        m_client.MoveResultReceived += result => RunOnUiThread(() =>
        {
            m_worldView.ApplyMoveResult(result);
            if (!result.Succeeded || result.IsCorrected)
            {
                AppendLog($"MoveRp sequence={result.Sequence}, result={result.ResultCode}, corrected={result.IsCorrected}");
            }
        });
        m_client.MoveNotificationReceived += notification => RunOnUiThread(() =>
        {
            m_worldView.ApplyMoveNotification(notification);
            UpdateStatus();
        });
        m_client.BasicAttackResultReceived += result => RunOnUiThread(() =>
        {
            if (result.Succeeded)
            {
                AppendLog($"BasicAttack accepted: sequence={result.AttackSequence}, tick={result.ServerTick}");
            }
            else
            {
                AppendLog(
                    $"BasicAttack rejected: result={result.ResultCode}, " +
                    $"sequence={result.AttackSequence}, tick={result.ServerTick}");
            }
        });
        m_client.ActorAttackReceived += notification => RunOnUiThread(() =>
        {
            if (!m_worldView.ApplyAttackNotification(notification))
            {
                return;
            }

            AppendLog(
                $"Attack attacker={notification.AttackerEntityId}, target={notification.TargetEntityId}, " +
                $"damage={notification.Damage:N0}, hp={notification.TargetCurrentHp:N0}/{notification.TargetMaxHp:N0}, " +
                $"tick={notification.ServerTick}");
            if (notification.TargetEntityId == m_worldView.LocalEntityId)
            {
                m_hpLabel.Text = $"{m_worldView.LocalCurrentHp:N0} / {m_worldView.LocalMaxHp:N0}";
                m_hpLabel.ForeColor = m_worldView.LocalCurrentHp == 0 ? Color.Salmon : Color.White;
            }
            UpdateStatus();
        });
        m_client.ActorDeathReceived += notification => RunOnUiThread(() =>
        {
            if (!m_worldView.ApplyDeathNotification(notification))
            {
                return;
            }

            AppendLog(
                $"Death entity={notification.EntityId}, killer={notification.KillerEntityId}, " +
                $"life={notification.LifeRevision}, tick={notification.ServerTick}");
            if (notification.EntityId == m_worldView.LocalEntityId)
            {
                m_pressedMovementKeys.Clear();
                m_attackKeyPressed = false;
                m_localMoving = false;
                m_hpLabel.Text = $"0 / {m_worldView.LocalMaxHp:N0}";
                m_hpLabel.ForeColor = Color.Salmon;
            }
            UpdateStatus();
            UpdateUiState();
        });
        m_worldView.TargetSelectionChanged += target =>
        {
            if (target.HasValue)
            {
                WorldActorSnapshot selected = target.Value;
                AppendLog(
                    $"Target selected: entity={selected.EntityId}, data={selected.ActorDataId}, " +
                    $"hp={selected.CurrentHp:N0}/{selected.MaxHp:N0}");
            }
            else
            {
                AppendLog("Target cleared.");
            }
            UpdateStatus();
        };
        m_client.ActorRespawnReceived += notification => RunOnUiThread(() =>
        {
            if (!m_worldView.ApplyRespawnNotification(notification))
            {
                return;
            }

            AppendLog(
                $"Respawn entity={notification.EntityId}, pos=({notification.PositionX:F1}, {notification.PositionY:F1}), " +
                $"hp={notification.CurrentHp:N0}/{notification.MaxHp:N0}, life={notification.LifeRevision}, " +
                $"tick={notification.ServerTick}");
            if (notification.EntityId == m_worldView.LocalEntityId)
            {
                m_pressedMovementKeys.Clear();
                m_localMoving = false;
                m_hpLabel.Text = $"{notification.CurrentHp:N0} / {notification.MaxHp:N0}";
                m_hpLabel.ForeColor = Color.White;
            }
            UpdateStatus();
            UpdateUiState();
        });
    }

    private async Task ConnectAsync()
    {
        if (m_authOperationPending)
        {
            return;
        }

        string loginId = m_loginIdTextBox.Text.Trim();
        string password = m_passwordTextBox.Text;
        if (string.IsNullOrWhiteSpace(loginId) || string.IsNullOrEmpty(password))
        {
            SetLoginStatus("아이디와 비밀번호를 입력해 주세요.", isError: true);
            return;
        }

        SetAuthControlsEnabled(false);
        try
        {
            SetLoginStatus("LoginServer 인증 중...", isError: false);
            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(15.0));
            AppendLog($"LoginServer request: {m_settings.LoginServerBaseUrl}");
            WorldLoginResponse login = await m_loginApiClient.LoginAsync(
                new WorldLoginServerSettings(m_settings.LoginServerBaseUrl),
                new WorldLoginRequest(loginId, password),
                timeout.Token);
            AppendLog(
                $"Login succeeded: user={login.UserId}, world={login.WorldServer.Ip}:{login.WorldServer.Port}, " +
                $"instance={login.WorldServer.InstanceId}, expires={login.TicketExpiresInSeconds}s");

            SetLoginStatus("WorldServer 연결 및 인증 중...", isError: false);
            await m_client.ConnectAsync(new WorldConnectionSettings(
                login.WorldServer.Ip,
                login.WorldServer.Port,
                m_settings.WorldPacketKey), timeout.Token);
            WorldAuthResult auth = await m_client.AuthenticateAsync(login.WorldTicket, timeout.Token);
            if (!auth.Succeeded || auth.UserId != login.UserId)
            {
                throw new InvalidOperationException(
                    $"WorldAuth failed. result={auth.ResultCode}, responseUser={auth.UserId}, loginUser={login.UserId}");
            }

            m_isAuthenticated = true;
            m_isCompatibilityMode = false;
            m_userLabel.Text = $"{login.Nickname} · User {login.UserId}";
            AppendLog($"WorldAuth succeeded: user={auth.UserId}, request={auth.RequestId}");
            SetLoginStatus("Map 입장 중...", isError: false);
            m_loginFlowPending = true;
            UpdateStatus();
            await EnterMapAsync();
        }
        catch (Exception exception)
        {
            m_loginFlowPending = false;
            AppendLog($"Connect failed: {exception.Message}");
            SetLoginStatus($"로그인 실패 · {exception.Message}", isError: true);
            if (m_client.IsConnected)
            {
                m_preserveLoginStatusOnDisconnect = true;
                try
                {
                    await m_client.DisconnectAsync("Bootstrap failed.");
                }
                catch (Exception disconnectException)
                {
                    AppendLog($"Bootstrap cleanup failed: {disconnectException.Message}");
                }
            }
        }
        finally
        {
            if (!m_loginFlowPending)
            {
                SetAuthControlsEnabled(true);
            }
            UpdateUiState();
        }
    }

    private async Task DirectConnectAsync()
    {
        if (m_authOperationPending)
        {
            return;
        }

        SetAuthControlsEnabled(false);
        try
        {
            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(15.0));
            SetLoginStatus(
                $"개발용 WorldServer 직접 연결 중... {m_settings.WorldServerHost}:{m_settings.WorldServerPort}",
                isError: false);
            await m_client.ConnectAsync(new WorldConnectionSettings(
                m_settings.WorldServerHost,
                m_settings.WorldServerPort,
                m_settings.WorldPacketKey), timeout.Token);
            m_isAuthenticated = false;
            m_isCompatibilityMode = true;
            m_userLabel.Text = "Development compatibility";
            m_loginFlowPending = true;
            SetLoginStatus("개발용 Map 입장 중...", isError: false);
            await EnterMapAsync();
        }
        catch (Exception exception)
        {
            m_loginFlowPending = false;
            AppendLog($"Direct connect failed: {exception.Message}");
            SetLoginStatus($"직접 접속 실패 · {exception.Message}", isError: true);
            if (m_client.IsConnected)
            {
                m_preserveLoginStatusOnDisconnect = true;
                await m_client.DisconnectAsync("Direct bootstrap failed.");
            }
        }
        finally
        {
            if (!m_loginFlowPending)
            {
                SetAuthControlsEnabled(true);
            }
            UpdateUiState();
        }
    }

    private async Task RegisterAsync()
    {
        if (m_authOperationPending)
        {
            return;
        }

        WorldRegisterRequest request = new(
            m_registerIdTextBox.Text.Trim(),
            m_registerPasswordTextBox.Text,
            m_registerNicknameTextBox.Text.Trim());
        if (string.IsNullOrWhiteSpace(request.LoginId) ||
            string.IsNullOrEmpty(request.Password) ||
            string.IsNullOrWhiteSpace(request.Nickname))
        {
            SetLoginStatus("회원가입 정보를 모두 입력해 주세요.", isError: true);
            return;
        }

        SetAuthControlsEnabled(false);
        try
        {
            SetLoginStatus("회원가입 처리 중...", isError: false);
            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(15.0));
            WorldRegisterResponse response = await m_loginApiClient.RegisterAsync(
                new WorldLoginServerSettings(m_settings.LoginServerBaseUrl),
                request,
                timeout.Token);

            m_loginIdTextBox.Text = request.LoginId;
            m_passwordTextBox.Text = request.Password;
            m_registerPasswordTextBox.Clear();
            m_authTabControl.SelectedIndex = 0;
            SetLoginStatus($"회원가입 완료 · User {response.UserId} · 로그인 버튼을 눌러 주세요.", isError: false);
            AppendLog($"Register succeeded: user={response.UserId}, nickname={response.Nickname}");
            MessageBox.Show(
                this,
                $"회원가입이 완료되었습니다.\r\n아이디: {request.LoginId}\r\n닉네임: {response.Nickname}\r\n\r\n자동 로그인하지 않았습니다. 로그인 버튼을 눌러 주세요.",
                "회원가입 성공",
                MessageBoxButtons.OK,
                MessageBoxIcon.Information);
        }
        catch (Exception exception)
        {
            AppendLog($"Register failed: {exception.Message}");
            SetLoginStatus($"회원가입 실패 · {exception.Message}", isError: true);
            MessageBox.Show(this, exception.Message, "회원가입 실패", MessageBoxButtons.OK, MessageBoxIcon.Warning);
        }
        finally
        {
            SetAuthControlsEnabled(true);
        }
    }

    private async Task DisconnectAsync()
    {
        SetToolbarBusy(true);
        try
        {
            await m_client.DisconnectAsync("Disconnected by user.");
        }
        catch (Exception exception)
        {
            AppendLog($"Disconnect failed: {exception.Message}");
        }
        finally
        {
            SetToolbarBusy(false);
            UpdateUiState();
        }
    }

    private async Task EnterMapAsync()
    {
        if (!m_client.IsConnected || m_mapEnterPending)
        {
            return;
        }

        m_mapEnterPending = true;
        UpdateUiState();
        ulong requestId = m_nextRequestId++;
        uint mapDataId = decimal.ToUInt32(m_mapDataIdInput.Value);
        AppendLog($"MapEnterRq request={requestId}, MapDataId={mapDataId}");
        try
        {
            await m_client.SendMapEnterAsync(requestId, mapDataId);
        }
        catch (Exception exception)
        {
            m_mapEnterPending = false;
            AppendLog($"MapEnter send failed: {exception.Message}");
            if (m_loginFlowPending)
            {
                m_loginFlowPending = false;
                SetLoginStatus($"Map 입장 실패 · {exception.Message}", isError: true);
                SetAuthControlsEnabled(true);
                if (m_client.IsConnected)
                {
                    m_preserveLoginStatusOnDisconnect = true;
                    await m_client.DisconnectAsync("Map bootstrap failed.");
                }
            }
            UpdateUiState();
        }
    }

    private void HandleConnectionChanged(bool connected)
    {
        m_isConnected = connected;
        if (!connected)
        {
            bool wasLoginFlowPending = m_loginFlowPending;
            m_loginFlowPending = false;
            m_mapEnterPending = false;
            m_equipmentRequestPending = false;
            m_hasPlayerStats = false;
            m_isAuthenticated = false;
            m_isCompatibilityMode = false;
            m_pressedMovementKeys.Clear();
            m_attackKeyPressed = false;
            m_localMoving = false;
            m_worldView.ClearWorld();
            ClearPlayerStats();
            m_mainPanel.Visible = false;
            m_loginPanel.Visible = true;
            m_loginPanel.BringToFront();
            if (!m_closing)
            {
                if (!m_preserveLoginStatusOnDisconnect)
                {
                    SetLoginStatus(
                        wasLoginFlowPending ? "연결이 종료되어 로그인을 완료하지 못했습니다." : "연결이 종료되었습니다. 다시 로그인해 주세요.",
                        isError: wasLoginFlowPending);
                }
                m_preserveLoginStatusOnDisconnect = false;
                SetAuthControlsEnabled(true);
            }
        }
        UpdateStatus();
        UpdateUiState();
    }

    private void HandleMapEnterResult(MapEnterResult result)
    {
        m_mapEnterPending = false;
        if (result.Succeeded)
        {
            m_pressedMovementKeys.Clear();
            m_attackKeyPressed = false;
            m_localMoving = false;
            m_worldView.EnterLocal(result);
            if (result.HasPlayerSnapshot)
            {
                ApplyPlayerStats(
                    result.FinalStr,
                    result.FinalDex,
                    result.FinalIntelligence,
                    result.FinalLuk,
                    result.CurrentHp,
                    result.MaxHp,
                    result.CurrentMp,
                    result.MaxMp,
                    result.Attack,
                    result.Defense,
                    result.MoveSpeedMilli,
                    result.EquipmentVersion,
                    result.StatRevision);
            }
            else
            {
                m_hasPlayerStats = false;
                ClearPlayerStats();
            }
            AppendLog($"MapEnter success: instance={result.MapInstanceId}, entity={result.EntityId}");
            if (m_loginFlowPending)
            {
                m_loginFlowPending = false;
                SetLoginStatus("로그인 완료", isError: false);
                ShowMainView();
                SetAuthControlsEnabled(true);
            }
        }
        else
        {
            AppendLog($"MapEnter failed: result={result.ResultCode}, request={result.RequestId}");
            if (m_loginFlowPending)
            {
                m_loginFlowPending = false;
                SetLoginStatus($"Map 입장 실패 · ResultCode {result.ResultCode}", isError: true);
                SetAuthControlsEnabled(true);
                m_preserveLoginStatusOnDisconnect = true;
                _ = m_client.DisconnectAsync("MapEnter rejected.");
            }
        }
        UpdateStatus();
        UpdateUiState();
    }

    private async Task ExecuteEquipmentMutationAsync(bool equip)
    {
        if (!m_client.IsConnected || m_worldView.LocalEntityId == 0 || m_equipmentRequestPending)
        {
            return;
        }

        ulong itemInstanceId = decimal.ToUInt64(m_itemInstanceIdInput.Value);
        ulong expectedVersion = decimal.ToUInt64(m_expectedItemVersionInput.Value);
        m_equipmentRequestPending = true;
        UpdateUiState();
        AppendLog($"{(equip ? "EquipItemRq" : "UnequipItemRq")}: item={itemInstanceId}, expectedVersion={expectedVersion}");
        try
        {
            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(15.0));
            _ = equip
                ? await m_client.EquipItemAsync(itemInstanceId, expectedVersion, timeout.Token)
                : await m_client.UnequipItemAsync(itemInstanceId, expectedVersion, timeout.Token);
        }
        catch (Exception exception)
        {
            AppendLog($"Equipment request failed: {exception.Message}");
        }
        finally
        {
            m_equipmentRequestPending = false;
            UpdateUiState();
        }
    }

    private void HandleEquipmentMutationResult(EquipmentMutationResult result)
    {
        if (!result.Succeeded)
        {
            AppendLog(
                $"Equipment command rejected: result={result.ResultCode}, request={result.RequestId}, item={result.ItemInstanceId}");
            return;
        }

        ApplyPlayerStats(
            result.FinalStr,
            result.FinalDex,
            result.FinalIntelligence,
            result.FinalLuk,
            result.CurrentHp,
            result.MaxHp,
            result.CurrentMp,
            result.MaxMp,
            result.Attack,
            result.Defense,
            result.MoveSpeedMilli,
            result.EquipmentVersion,
            result.StatRevision);
        if (decimal.ToUInt64(m_itemInstanceIdInput.Value) == result.ItemInstanceId && result.ItemVersion != 0)
        {
            m_expectedItemVersionInput.Value = result.ItemVersion;
        }
        AppendLog(
            $"Equipment command success: item={result.ItemInstanceId}, version={result.ItemVersion}, " +
            $"equipped={result.Equipped}, attack={result.Attack}, statRevision={result.StatRevision}");
    }

    private void ApplyPlayerStats(
        uint finalStr,
        uint finalDex,
        uint finalIntelligence,
        uint finalLuk,
        uint currentHp,
        uint maxHp,
        uint currentMp,
        uint maxMp,
        uint attack,
        uint defense,
        uint moveSpeedMilli,
        ulong equipmentVersion,
        ulong statRevision)
    {
        m_hasPlayerStats = true;
        m_hpLabel.Text = $"{currentHp:N0} / {maxHp:N0}";
        m_hpLabel.ForeColor = currentHp == 0 ? Color.Salmon : Color.White;
        m_mpLabel.Text = $"{currentMp:N0} / {maxMp:N0}";
        m_finalStrDexLabel.Text = $"{finalStr:N0} / {finalDex:N0}";
        m_finalIntLukLabel.Text = $"{finalIntelligence:N0} / {finalLuk:N0}";
        m_attackDefenseLabel.Text = $"{attack:N0} / {defense:N0}";
        m_moveSpeedLabel.Text = $"{moveSpeedMilli / 1000.0:F3} ({moveSpeedMilli:N0} milli)";
        m_worldView.SetLocalMoveSpeed(moveSpeedMilli);
        m_worldView.SetLocalHealth(currentHp, maxHp);
        m_equipmentVersionLabel.Text = equipmentVersion.ToString();
        m_statRevisionLabel.Text = statRevision.ToString();
        UpdateUiState();
    }

    private void ClearPlayerStats()
    {
        m_hpLabel.Text = "-";
        m_hpLabel.ForeColor = Color.White;
        m_lifeStateLabel.Text = "-";
        m_lifeStateLabel.ForeColor = Color.White;
        m_mpLabel.Text = "-";
        m_finalStrDexLabel.Text = "-";
        m_finalIntLukLabel.Text = "-";
        m_attackDefenseLabel.Text = "-";
        m_moveSpeedLabel.Text = "-";
        m_equipmentVersionLabel.Text = "-";
        m_statRevisionLabel.Text = "-";
    }

    private void UpdateMovementFromKeys()
    {
        if (!m_isConnected || m_worldView.LocalEntityId == 0 || m_worldView.LocalIsDead)
        {
            return;
        }

        float directionX = 0.0f;
        float directionY = 0.0f;
        if (IsPressed(Keys.A, Keys.Left)) directionX -= 1.0f;
        if (IsPressed(Keys.D, Keys.Right)) directionX += 1.0f;
        if (IsPressed(Keys.W, Keys.Up)) directionY -= 1.0f;
        if (IsPressed(Keys.S, Keys.Down)) directionY += 1.0f;

        if (directionX == 0.0f && directionY == 0.0f)
        {
            StopMovement(clearPressedKeys: false);
            return;
        }

        float length = MathF.Sqrt(directionX * directionX + directionY * directionY);
        directionX /= length;
        directionY /= length;
        bool directionChanged = Math.Abs(directionX - m_currentDirectionX) > 0.001f ||
            Math.Abs(directionY - m_currentDirectionY) > 0.001f;
        EWorldMoveState state = m_localMoving ? EWorldMoveState.Sync : EWorldMoveState.Start;
        if (!m_localMoving || directionChanged)
        {
            SendMove(state, directionX, directionY);
        }
        m_localMoving = true;
        m_currentDirectionX = directionX;
        m_currentDirectionY = directionY;
    }

    private void StopMovement(bool clearPressedKeys = true)
    {
        if (clearPressedKeys)
        {
            m_pressedMovementKeys.Clear();
        }
        if (!m_localMoving)
        {
            return;
        }

        SendMove(EWorldMoveState.Stop, m_currentDirectionX, m_currentDirectionY);
        m_localMoving = false;
    }

    private void SendMove(EWorldMoveState state, float directionX, float directionY)
    {
        if (!m_worldView.TryGetLocalSnapshot(out float positionX, out float positionY, out _, out _))
        {
            return;
        }

        uint sequence = NextMoveSequence();
        if (!m_client.TrySendMove(sequence, state, positionX, positionY, directionX, directionY))
        {
            AppendLog("Move send failed: not connected.");
            if (state == EWorldMoveState.Stop)
            {
                m_worldView.SetLocalInput(directionX, directionY, EWorldMoveState.Stop, sequence);
            }
            return;
        }

        m_worldView.SetLocalInput(directionX, directionY, state, sequence);
        m_lastMoveSyncTime = m_frameClock.Elapsed;
    }

    private void SendBasicAttack()
    {
        if (!m_isConnected || m_worldView.LocalEntityId == 0)
        {
            AppendLog("Attack unavailable: enter a map first.");
            return;
        }
        if (m_worldView.LocalIsDead)
        {
            AppendLog("Attack unavailable: player is dead.");
            return;
        }
        if (!m_worldView.TryGetSelectedLivingMonster(out WorldActorSnapshot target))
        {
            AppendLog("Attack unavailable: click a living monster first.");
            return;
        }

        uint sequence = NextAttackSequence();
        if (!m_client.TrySendBasicAttack(sequence, target.EntityId))
        {
            AppendLog($"BasicAttack send failed: sequence={sequence}, target={target.EntityId}");
            return;
        }

        AppendLog($"BasicAttackRq sequence={sequence}, target={target.EntityId}");
    }

    private void RenderTick()
    {
        TimeSpan now = m_frameClock.Elapsed;
        TimeSpan elapsed = now - m_lastFrameTime;
        m_lastFrameTime = now;
        m_worldView.Advance(elapsed);

        if (m_localMoving && !m_worldView.LocalIsDead && now - m_lastMoveSyncTime >= s_moveSyncInterval)
        {
            SendMove(EWorldMoveState.Sync, m_currentDirectionX, m_currentDirectionY);
        }
        UpdateStatus();
    }

    private TimeSpan m_lastFrameTime;

    private uint NextMoveSequence()
    {
        m_moveSequence++;
        if (m_moveSequence == 0)
        {
            m_moveSequence = 1;
        }
        return m_moveSequence;
    }

    private uint NextAttackSequence()
    {
        m_attackSequence++;
        if (m_attackSequence == 0)
        {
            m_attackSequence = 1;
        }
        return m_attackSequence;
    }

    private bool IsPressed(Keys primary, Keys secondary) =>
        m_pressedMovementKeys.Contains(primary) || m_pressedMovementKeys.Contains(secondary);

    private void UpdateStatus()
    {
        m_connectionLabel.Text = m_isConnected ? "Connected" : "Disconnected";
        m_connectionLabel.ForeColor = m_isConnected ? Color.LightGreen : Color.Salmon;
        m_authLabel.Text = m_isAuthenticated ? "WorldAuth OK" : m_isCompatibilityMode ? "Compatibility" : "-";
        m_authLabel.ForeColor = m_isAuthenticated
            ? Color.LightGreen
            : m_isCompatibilityMode ? Color.Khaki : Color.White;
        m_mapLabel.Text = m_worldView.MapInstanceId == 0 ? "-" : m_worldView.MapInstanceId.ToString();
        m_entityLabel.Text = m_worldView.LocalEntityId == 0 ? "-" : m_worldView.LocalEntityId.ToString();
        m_actorCountLabel.Text = m_worldView.ActorCount.ToString();
        m_targetLabel.Text = m_worldView.TryGetSelectedLivingMonster(out WorldActorSnapshot target)
            ? $"{target.EntityId} · HP {target.CurrentHp:N0}/{target.MaxHp:N0}"
            : "-";
        if (m_worldView.LocalEntityId == 0)
        {
            m_lifeStateLabel.Text = "-";
            m_lifeStateLabel.ForeColor = Color.White;
        }
        else
        {
            m_lifeStateLabel.Text = m_worldView.LocalIsDead
                ? $"Dead · rev {m_worldView.LocalLifeRevision}"
                : $"Alive · rev {m_worldView.LocalLifeRevision}";
            m_lifeStateLabel.ForeColor = m_worldView.LocalIsDead ? Color.Salmon : Color.LightGreen;
        }
        m_positionLabel.Text = m_worldView.TryGetLocalSnapshot(out float x, out float y, out _, out _)
            ? $"{x:F1}, {y:F1}"
            : "-";
    }

    private void SetToolbarBusy(bool busy)
    {
        m_connectButton.Enabled = !busy;
        m_disconnectButton.Enabled = !busy;
        m_enterMapButton.Enabled = !busy;
        m_loginIdTextBox.Enabled = !busy;
        m_passwordTextBox.Enabled = !busy;
    }

    private void SetAuthControlsEnabled(bool enabled)
    {
        m_authOperationPending = !enabled;
        m_authTabControl.Enabled = enabled;
        m_connectButton.Enabled = enabled && !m_client.IsConnected;
        m_registerButton.Enabled = enabled && !m_client.IsConnected;
        m_directConnectButton.Enabled = enabled && !m_client.IsConnected;
        UseWaitCursor = !enabled;
    }

    private void SetLoginStatus(string message, bool isError)
    {
        m_loginStatusLabel.Text = $"● {message}";
        m_loginStatusLabel.ForeColor = isError ? Color.Salmon : s_muted;
    }

    private void UpdateUiState()
    {
        bool connected = m_client.IsConnected;
        m_connectButton.Enabled = !connected && !m_authOperationPending;
        m_registerButton.Enabled = !connected && !m_authOperationPending;
        m_directConnectButton.Enabled = !connected && !m_authOperationPending;
        m_disconnectButton.Enabled = connected;
        m_enterMapButton.Enabled = connected && !m_mapEnterPending;
        m_mapDataIdInput.Enabled = !m_mapEnterPending;
        m_loginIdTextBox.Enabled = !connected && !m_authOperationPending;
        m_passwordTextBox.Enabled = !connected && !m_authOperationPending;
        bool equipmentEnabled = connected &&
            m_worldView.LocalEntityId != 0 &&
            !m_worldView.LocalIsDead &&
            m_hasPlayerStats &&
            !m_equipmentRequestPending;
        m_itemInstanceIdInput.Enabled = equipmentEnabled;
        m_expectedItemVersionInput.Enabled = equipmentEnabled;
        m_equipButton.Enabled = equipmentEnabled;
        m_unequipButton.Enabled = equipmentEnabled;
    }

    private void AppendLog(string message)
    {
        if (m_logTextBox.TextLength > 100_000)
        {
            m_logTextBox.Clear();
        }
        m_logTextBox.AppendText($"[{DateTime.Now:HH:mm:ss.fff}] {message}{Environment.NewLine}");
        m_logTextBox.SelectionStart = m_logTextBox.TextLength;
        m_logTextBox.ScrollToCaret();
    }

    private void RunOnUiThread(Action action)
    {
        if (m_closing || IsDisposed)
        {
            return;
        }
        if (InvokeRequired)
        {
            try
            {
                BeginInvoke(action);
            }
            catch (InvalidOperationException)
            {
            }
            return;
        }
        action();
    }

    private static bool IsMovementKey(Keys key) => key is
        Keys.W or Keys.A or Keys.S or Keys.D or Keys.Up or Keys.Down or Keys.Left or Keys.Right;

    private static Label CreateCaption(string text) => new()
    {
        Text = text,
        AutoSize = true,
        ForeColor = Color.FromArgb(180, 195, 205),
        Padding = new Padding(7, 7, 5, 0)
    };

    private static Label CreateValueLabel(string text) => new()
    {
        Text = text,
        AutoSize = true,
        ForeColor = Color.White,
        Padding = new Padding(5)
    };

    private static void AddStatusRow(TableLayoutPanel panel, int row, string name, Control value)
    {
        panel.RowCount = Math.Max(panel.RowCount, row + 1);
        panel.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        Label caption = CreateCaption(name);
        panel.Controls.Add(caption, 0, row);
        panel.Controls.Add(value, 1, row);
    }

    private static void AddSectionHeader(TableLayoutPanel panel, int row, string text)
    {
        panel.RowCount = Math.Max(panel.RowCount, row + 1);
        panel.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        Label label = new()
        {
            Text = text,
            AutoSize = true,
            Font = new Font("Segoe UI Semibold", 10.0f),
            ForeColor = Color.FromArgb(240, 194, 83),
            Padding = new Padding(5, row == 0 ? 2 : 14, 5, 5)
        };
        panel.Controls.Add(label, 0, row);
        panel.SetColumnSpan(label, 2);
    }
}
