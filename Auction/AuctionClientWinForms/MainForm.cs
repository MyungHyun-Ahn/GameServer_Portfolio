using AuctionClientWinForms.Controls;
using AuctionClientWinForms.Configuration;
using AuctionClientWinForms.Models;
using AuctionClientWinForms.Networking;
using System.Text.Json;

namespace AuctionClientWinForms;

internal sealed class MainForm : Form
{
	private enum SearchSortType : byte
	{
		Newest = 1,
		PriceAscending = 2,
		PriceDescending = 3,
		ExpiringSoon = 4
	}

    private static readonly Color s_background = Color.FromArgb(15, 20, 26);
    private static readonly Color s_panel = Color.FromArgb(24, 31, 39);
    private static readonly Color s_panelLight = Color.FromArgb(33, 42, 52);
    private static readonly Color s_border = Color.FromArgb(71, 79, 88);
    private static readonly Color s_gold = Color.FromArgb(198, 155, 75);
    private static readonly Color s_text = Color.FromArgb(232, 235, 238);
    private static readonly Color s_muted = Color.FromArgb(160, 169, 178);

    private readonly AuthApiClient m_authApiClient = new();
    private readonly AuctionTcpClient m_client = new();
    private readonly ClientSettings m_settings;

    private readonly Panel m_loginPanel = new() { Dock = DockStyle.Fill, AutoScroll = true };
    private readonly Panel m_mainPanel = new() { Dock = DockStyle.Fill, Visible = false };
    private readonly TabControl m_authTabControl = new() { Dock = DockStyle.Fill };
    private readonly TextBox m_loginIdTextBox = new() { Dock = DockStyle.Fill };
    private readonly TextBox m_passwordTextBox = new() { Dock = DockStyle.Fill, UseSystemPasswordChar = true };
    private readonly Button m_loginButton = new() { Text = "로그인" };
    private readonly TextBox m_registerIdTextBox = new() { Dock = DockStyle.Fill };
    private readonly TextBox m_registerPasswordTextBox = new() { Dock = DockStyle.Fill, UseSystemPasswordChar = true };
    private readonly TextBox m_registerNicknameTextBox = new() { Dock = DockStyle.Fill };
    private readonly Button m_registerButton = new() { Text = "회원가입" };
    private readonly Label m_loginServerLabel = new() { AutoSize = true };
    private readonly Label m_loginStatusLabel = new() { Text = "● 로그인 또는 회원가입을 선택하세요", AutoSize = true };

    private readonly Label m_userLabel = new() { AutoSize = true };
    private readonly Label m_connectionLabel = new() { Text = "● 연결 안 됨", AutoSize = true };
    private readonly Label m_balanceLabel = new() { Text = "재화 -", AutoSize = true };
    private readonly NavigationButton m_auctionButton = new() { Text = "⚒  경매장" };
    private readonly NavigationButton m_saleHistoryButton = new() { Text = "▤  판매 이력" };
    private readonly NavigationButton m_myListingsButton = new() { Text = "▣  내 판매" };
    private readonly NavigationButton m_myBidsButton = new() { Text = "◆  내 입찰" };
    private readonly NavigationButton m_mailButton = new() { Text = "✉  우편함" };
    private readonly NavigationButton m_sellButton = new() { Text = "＋  판매 등록" };
    private readonly Panel m_contentHost = new() { Dock = DockStyle.Fill, Padding = new Padding(18) };
    private readonly Label m_toastLabel = new() { Visible = false, AutoSize = false, Height = 44, TextAlign = ContentAlignment.MiddleCenter };
    private readonly System.Windows.Forms.Timer m_toastTimer = new() { Interval = 4500 };

    private readonly Panel m_auctionView = new() { Dock = DockStyle.Fill };
    private readonly ComboBox m_categoryCombo = new() { DropDownStyle = ComboBoxStyle.DropDownList };
    private readonly TextBox m_itemNameInput = new() { Dock = DockStyle.Fill };
	private readonly ComboBox m_listingSortCombo = new() { DropDownStyle = ComboBoxStyle.DropDownList };
    private readonly NumericUpDown m_strInput = CreateNumericInput(uint.MaxValue);
    private readonly NumericUpDown m_dexInput = CreateNumericInput(uint.MaxValue);
    private readonly NumericUpDown m_intInput = CreateNumericInput(uint.MaxValue);
    private readonly NumericUpDown m_lukInput = CreateNumericInput(uint.MaxValue);
    private readonly Button m_searchButton = new() { Text = "검색" };
    private readonly DataGridView m_listingGrid = CreateGrid();
    private readonly Label m_detailNameLabel = new() { Text = "매물을 선택하세요", AutoSize = true };
    private readonly Label m_detailStatsLabel = new() { Text = "", AutoSize = true };
    private readonly Label m_detailPriceLabel = new() { Text = "", AutoSize = true };
    private readonly NumericUpDown m_bidAmountInput = CreateNumericInput(decimal.MaxValue);
    private readonly Button m_bidButton = new() { Text = "입찰" };
    private readonly Button m_buyoutButton = new() { Text = "즉시 구매" };
	private readonly Button m_listingPrevButton = new() { Text = "이전", Enabled = false };
	private readonly Button m_listingNextButton = new() { Text = "다음", Enabled = false };
	private readonly Label m_listingPageLabel = new() { Text = "페이지 1", AutoSize = true, TextAlign = ContentAlignment.MiddleCenter };

    private readonly Panel m_saleHistoryView = new() { Dock = DockStyle.Fill };
    private readonly ComboBox m_historyCategoryCombo = new() { DropDownStyle = ComboBoxStyle.DropDownList };
    private readonly TextBox m_historyItemNameInput = new() { Dock = DockStyle.Fill };
	private readonly ComboBox m_historySortCombo = new() { DropDownStyle = ComboBoxStyle.DropDownList };
    private readonly NumericUpDown m_historyStrInput = CreateNumericInput(uint.MaxValue);
    private readonly NumericUpDown m_historyDexInput = CreateNumericInput(uint.MaxValue);
    private readonly NumericUpDown m_historyIntInput = CreateNumericInput(uint.MaxValue);
    private readonly NumericUpDown m_historyLukInput = CreateNumericInput(uint.MaxValue);
    private readonly Button m_historySearchButton = new() { Text = "검색" };
    private readonly DataGridView m_saleHistoryGrid = CreateGrid();
    private readonly Label m_historySummaryLabel = new() { Text = "최근 거래를 검색하세요", AutoSize = true };
    private readonly Label m_historyDetailNameLabel = new() { Text = "거래 이력을 선택하세요", AutoSize = true };
    private readonly Label m_historyDetailStatsLabel = new() { AutoSize = true };
    private readonly Label m_historyDetailPriceLabel = new() { AutoSize = true };
    private readonly Label m_historyDetailSellerLabel = new() { AutoSize = true };
	private readonly Button m_historyPrevButton = new() { Text = "이전", Enabled = false };
	private readonly Button m_historyNextButton = new() { Text = "다음", Enabled = false };
	private readonly Label m_historyPageLabel = new() { Text = "페이지 1", AutoSize = true, TextAlign = ContentAlignment.MiddleCenter };

    private readonly Panel m_myBidsView = new() { Dock = DockStyle.Fill };
    private readonly DataGridView m_bidGrid = CreateGrid();
    private readonly Button m_refreshBidsButton = new() { Text = "새로고침" };
    private readonly Button m_refundBidButton = new() { Text = "재화 회수", Enabled = false };

    private readonly Panel m_myListingsView = new() { Dock = DockStyle.Fill };
    private readonly DataGridView m_myListingsGrid = CreateGrid();
    private readonly Button m_refreshMyListingsButton = new() { Text = "새로고침" };
    private readonly Label m_myListingDetailNameLabel = new() { Text = "매물을 선택하세요", AutoSize = true };
    private readonly Label m_myListingDetailStatsLabel = new() { AutoSize = true };
    private readonly Label m_myListingDetailPriceLabel = new() { AutoSize = true };
    private readonly Label m_myListingCancelStateLabel = new() { AutoSize = true };
    private readonly Button m_cancelListingButton = new() { Text = "판매 취소", Enabled = false };

    private readonly Panel m_mailView = new() { Dock = DockStyle.Fill };
    private readonly DataGridView m_mailGrid = CreateGrid();
    private readonly Label m_mailSubjectLabel = new() { Text = "우편을 선택하세요", AutoSize = true };
    private readonly TextBox m_mailBodyTextBox = new() { Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Vertical };
    private readonly DataGridView m_attachmentGrid = CreateGrid();
    private readonly Label m_attachmentDetailLabel = new() { Text = "첨부물을 선택하세요", AutoSize = true };
    private readonly Button m_refreshMailButton = new() { Text = "새로고침" };
    private readonly Button m_claimButton = new() { Text = "첨부물 수령", Enabled = false };

    private readonly Panel m_sellView = new() { Dock = DockStyle.Fill };
    private readonly DataGridView m_inventoryGrid = CreateGrid();
    private readonly Label m_inventoryDetailNameLabel = new() { Text = "아이템을 선택하세요", AutoSize = true };
    private readonly Label m_inventoryDetailStatsLabel = new() { AutoSize = true };
    private readonly Label m_inventoryDetailStateLabel = new() { AutoSize = true };
    private readonly NumericUpDown m_startPriceInput = CreateNumericInput(decimal.MaxValue);
    private readonly NumericUpDown m_buyoutPriceInput = CreateNumericInput(decimal.MaxValue);
    private readonly NumericUpDown m_durationInput = CreateNumericInput(uint.MaxValue);
    private readonly Button m_refreshInventoryButton = new() { Text = "인벤토리 새로고침" };
    private readonly Button m_registerListingButton = new() { Text = "경매장에 등록" };
	private readonly Label m_listingLimitLabel = new() { Text = "판매 등록 - / -", AutoSize = true };

    private readonly Label m_statusStripLabel = new() { Text = "준비", AutoSize = true };
    private readonly TextBox m_cheatInput = new()
    {
        Width = 520,
        PlaceholderText = "/gold n 또는 /item item_data_id str dex int luk"
    };
    private readonly Button m_cheatButton = new() { Text = "치트 실행", Width = 96 };
    private ListingDetail? m_selectedListing;
    private ListingDetail? m_selectedMyListing;
    private MailDetail? m_selectedMail;
    private bool m_isUpdatingListingGrid;
    private bool m_isUpdatingSaleHistoryGrid;
    private bool m_isUpdatingMyListingsGrid;
    private bool m_isUpdatingMailGrid;
    private bool m_isUpdatingAttachmentGrid;
    private bool m_isUpdatingInventoryGrid;
	private readonly List<(ulong SortValue, ulong ListingId)> m_listingPageCursors = [(0, 0)];
	private readonly List<(ulong SortValue, ulong ListingId)> m_historyPageCursors = [(0, 0)];
	private int m_listingPageIndex;
	private int m_historyPageIndex;
	private bool m_listingHasNextPage;
	private bool m_historyHasNextPage;
	private int m_activeListingCount;
	private uint m_maxActiveListings;
	private int m_searchPageSize;
	private ushort m_defaultCurrencyId;
	private ulong m_minimumBidIncrement;
	private ulong m_minimumListingPrice;
	private ulong m_maximumListingPrice;
    private ulong m_userId;
    private string m_nickname = string.Empty;

    public MainForm(ClientSettings settings)
    {
        m_settings = settings;
        Text = "Auction Client";
        StartPosition = FormStartPosition.CenterScreen;
        AutoScaleMode = AutoScaleMode.Dpi;
        MinimumSize = new Size(1200, 760);
        Size = new Size(1480, 880);
        BackColor = s_background;
        ForeColor = s_text;
        Font = new Font("맑은 고딕", 9.5F);

        BuildLoginView();
        BuildMainView();
        Controls.Add(m_mainPanel);
        Controls.Add(m_loginPanel);
        WireEvents();
        ApplyTheme(this);
    }

    protected override async void OnFormClosed(FormClosedEventArgs eventArgs)
    {
        await m_client.DisposeAsync();
        base.OnFormClosed(eventArgs);
    }

    private void BuildLoginView()
    {
        TableLayoutPanel root = new() { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 1, AutoScroll = true };
        root.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));

        Panel card = new() { Size = new Size(520, 550), Padding = new Padding(38, 24, 38, 24), BackColor = s_panel, Anchor = AnchorStyles.None };
        TableLayoutPanel fields = new() { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 5 };
        fields.RowStyles.Add(new RowStyle(SizeType.Absolute, 60));
        fields.RowStyles.Add(new RowStyle(SizeType.Absolute, 28));
        fields.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        fields.RowStyles.Add(new RowStyle(SizeType.Absolute, 32));
        fields.RowStyles.Add(new RowStyle(SizeType.Absolute, 36));

        fields.Controls.Add(new Label
        {
            Text = "경매장 로그인",
            Dock = DockStyle.Fill,
            Font = new Font("맑은 고딕", 22, FontStyle.Bold),
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
        m_loginStatusLabel.Anchor = AnchorStyles.Left;
        fields.Controls.Add(m_loginStatusLabel, 0, 3);
        fields.Controls.Add(new Label
        {
            Text = "인증 성공 후 LoginServer가 알려준 AuctionServer로 연결합니다.",
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
        TabPage page = new("로그인") { BackColor = s_panel, Padding = new Padding(22, 20, 22, 16) };
        TableLayoutPanel fields = CreateAuthFields(6);
        fields.RowStyles.Add(new RowStyle(SizeType.Absolute, 28));
        fields.RowStyles.Add(new RowStyle(SizeType.Absolute, 48));
        fields.RowStyles.Add(new RowStyle(SizeType.Absolute, 28));
        fields.RowStyles.Add(new RowStyle(SizeType.Absolute, 48));
        fields.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        fields.RowStyles.Add(new RowStyle(SizeType.Absolute, 50));
        fields.Controls.Add(CreateLabel("아이디"));
        fields.Controls.Add(m_loginIdTextBox);
        fields.Controls.Add(CreateLabel("비밀번호"));
        fields.Controls.Add(m_passwordTextBox);
        fields.Controls.Add(new Panel());
        m_loginButton.Dock = DockStyle.Fill;
        fields.Controls.Add(m_loginButton);
        page.Controls.Add(fields);
        return page;
    }

    private TabPage BuildRegisterTab()
    {
        TabPage page = new("회원가입") { BackColor = s_panel, Padding = new Padding(22, 16, 22, 16) };
        TableLayoutPanel fields = CreateAuthFields(8);
        for (int i = 0; i < 3; ++i)
        {
            fields.RowStyles.Add(new RowStyle(SizeType.Absolute, 26));
            fields.RowStyles.Add(new RowStyle(SizeType.Absolute, 44));
        }
        fields.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        fields.RowStyles.Add(new RowStyle(SizeType.Absolute, 50));
        fields.Controls.Add(CreateLabel("아이디"));
        fields.Controls.Add(m_registerIdTextBox);
        fields.Controls.Add(CreateLabel("비밀번호"));
        fields.Controls.Add(m_registerPasswordTextBox);
        fields.Controls.Add(CreateLabel("닉네임"));
        fields.Controls.Add(m_registerNicknameTextBox);
        fields.Controls.Add(new Panel());
        m_registerButton.Dock = DockStyle.Fill;
        fields.Controls.Add(m_registerButton);
        page.Controls.Add(fields);
        return page;
    }

    private static TableLayoutPanel CreateAuthFields(int rowCount)
    {
        TableLayoutPanel fields = new() { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = rowCount };
        fields.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        return fields;
    }

    private void BuildMainView()
    {
        m_mainPanel.Controls.Add(m_contentHost);
        m_mainPanel.Controls.Add(BuildNavigation());
        m_mainPanel.Controls.Add(BuildHeader());
        m_mainPanel.Controls.Add(BuildStatusStrip());

        BuildAuctionView();
        BuildSaleHistoryView();
        BuildMyListingsView();
        BuildMyBidsView();
        BuildMailView();
        BuildSellView();
        m_contentHost.Controls.Add(m_sellView);
        m_contentHost.Controls.Add(m_mailView);
        m_contentHost.Controls.Add(m_myBidsView);
        m_contentHost.Controls.Add(m_myListingsView);
        m_contentHost.Controls.Add(m_saleHistoryView);
        m_contentHost.Controls.Add(m_auctionView);

        m_toastLabel.BackColor = Color.FromArgb(45, 35, 24);
        m_toastLabel.ForeColor = Color.White;
        m_toastLabel.BorderStyle = BorderStyle.FixedSingle;
        m_toastLabel.Anchor = AnchorStyles.Bottom;
        m_toastLabel.Width = 410;
        m_toastLabel.Left = (m_mainPanel.ClientSize.Width - m_toastLabel.Width) / 2;
        m_toastLabel.Top = m_mainPanel.ClientSize.Height - 90;
        m_mainPanel.Controls.Add(m_toastLabel);
        m_toastLabel.BringToFront();
        m_mainPanel.Resize += (_, _) =>
        {
            m_toastLabel.Left = Math.Max(0, (m_mainPanel.ClientSize.Width - m_toastLabel.Width) / 2);
            m_toastLabel.Top = Math.Max(0, m_mainPanel.ClientSize.Height - 90);
        };
    }

    private Control BuildHeader()
    {
        TableLayoutPanel header = new() { Dock = DockStyle.Top, Height = 62, ColumnCount = 6, Padding = new Padding(20, 10, 20, 8), BackColor = s_panel };
        header.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        for (int i = 0; i < 5; ++i) header.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        header.Controls.Add(new Label { Text = "⚒  AUCTION HOUSE", AutoSize = true, Font = new Font("맑은 고딕", 18, FontStyle.Bold), ForeColor = s_gold }, 0, 0);
        header.Controls.Add(m_connectionLabel, 1, 0);
        header.Controls.Add(CreateSpacer(), 2, 0);
        header.Controls.Add(m_userLabel, 3, 0);
        header.Controls.Add(CreateSpacer(), 4, 0);
        header.Controls.Add(m_balanceLabel, 5, 0);
        return header;
    }

    private Control BuildNavigation()
    {
        FlowLayoutPanel navigation = new()
        {
            Dock = DockStyle.Left,
            Width = 190,
            FlowDirection = FlowDirection.TopDown,
            WrapContents = false,
            Padding = new Padding(10, 22, 10, 10),
            BackColor = s_panel
        };
        foreach (NavigationButton button in new[] { m_auctionButton, m_saleHistoryButton, m_myListingsButton, m_myBidsButton, m_mailButton, m_sellButton })
        {
            button.Width = 166;
            button.Height = 54;
            button.FlatStyle = FlatStyle.Flat;
            button.TextAlign = ContentAlignment.MiddleLeft;
            button.Padding = new Padding(16, 0, 0, 0);
            navigation.Controls.Add(button);
        }
        return navigation;
    }

    private Control BuildStatusStrip()
    {
        Panel status = new() { Dock = DockStyle.Bottom, Height = 48, Padding = new Padding(14, 7, 14, 7), BackColor = Color.FromArgb(10, 14, 18) };
        FlowLayoutPanel cheat = new() { Dock = DockStyle.Right, AutoSize = true, WrapContents = false };
        m_cheatInput.Height = 32;
        m_cheatButton.Height = 32;
        cheat.Controls.Add(m_cheatInput);
        cheat.Controls.Add(m_cheatButton);
        status.Controls.Add(cheat);
        status.Controls.Add(m_statusStripLabel);
        return status;
    }

    private void BuildAuctionView()
    {
        TableLayoutPanel root = new() { Dock = DockStyle.Fill, RowCount = 3, ColumnCount = 1 };
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 88));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 44));

        FlowLayoutPanel filters = new() { Dock = DockStyle.Fill, Padding = new Padding(0, 5, 0, 8) };
        m_categoryCombo.Items.AddRange(["전체", "장비", "소비", "재료"]);
        m_categoryCombo.SelectedIndex = 0;
		m_listingSortCombo.Items.AddRange(["최신 등록순", "낮은 현재가순", "높은 현재가순", "마감 임박순"]);
		m_listingSortCombo.SelectedIndex = 0;
        AddFilter(filters, "카테고리", m_categoryCombo, 100);
        AddFilter(filters, "아이템 이름", m_itemNameInput, 170);
		AddFilter(filters, "정렬", m_listingSortCombo, 125);
        AddFilter(filters, "최소 STR", m_strInput, 82);
        AddFilter(filters, "최소 DEX", m_dexInput, 82);
        AddFilter(filters, "최소 INT", m_intInput, 82);
        AddFilter(filters, "최소 LUK", m_lukInput, 82);
        m_searchButton.Width = 92;
        m_searchButton.Height = 35;
        m_searchButton.Margin = new Padding(0, 27, 0, 0);
        filters.Controls.Add(m_searchButton);

        SplitContainer split = new()
        {
            Dock = DockStyle.Fill,
            SplitterDistance = 800,
            FixedPanel = FixedPanel.Panel2
        };
        split.SizeChanged += (_, _) => AdjustAuctionSplit(split);
        ConfigureListingGrid();
        split.Panel1.Controls.Add(m_listingGrid);
        split.Panel2.Controls.Add(BuildListingDetailPanel());
        root.Controls.Add(filters, 0, 0);
        root.Controls.Add(split, 0, 1);
		root.Controls.Add(BuildPaginationBar(m_listingPrevButton, m_listingPageLabel, m_listingNextButton), 0, 2);
        m_auctionView.Controls.Add(root);
    }

    private static void AdjustAuctionSplit(SplitContainer split)
    {
        const int detailWidth = 340;
        const int minimumListingWidth = 560;
        int availableWidth = split.ClientSize.Width - split.SplitterWidth;
        if (availableWidth < minimumListingWidth + detailWidth)
        {
            return;
        }

        int distance = Math.Clamp(availableWidth - detailWidth, minimumListingWidth, availableWidth - detailWidth);
        if (split.SplitterDistance != distance)
        {
            split.SplitterDistance = distance;
        }
    }

    private Control BuildListingDetailPanel()
    {
        TableLayoutPanel panel = CreateCardLayout();
        panel.Controls.Add(new Label { Text = "매물 상세", AutoSize = true, Font = new Font(Font, FontStyle.Bold), ForeColor = s_gold });
        panel.Controls.Add(m_detailNameLabel);
        panel.Controls.Add(m_detailStatsLabel);
        panel.Controls.Add(m_detailPriceLabel);
        panel.Controls.Add(CreateLabel("입찰 금액"));
        panel.Controls.Add(m_bidAmountInput);
        TableLayoutPanel buttons = new() { Dock = DockStyle.Top, Height = 48, ColumnCount = 2, Margin = new Padding(0, 8, 0, 0) };
        buttons.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50));
        buttons.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50));
        m_bidButton.Dock = DockStyle.Fill;
        m_bidButton.Margin = new Padding(0, 0, 5, 0);
        m_buyoutButton.Dock = DockStyle.Fill;
        m_buyoutButton.Margin = new Padding(5, 0, 0, 0);
        buttons.Controls.Add(m_bidButton, 0, 0);
        buttons.Controls.Add(m_buyoutButton, 1, 0);
        panel.Controls.Add(buttons);
        return panel;
    }

    private void BuildSaleHistoryView()
    {
        TableLayoutPanel root = new() { Dock = DockStyle.Fill, RowCount = 4, ColumnCount = 1 };
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 88));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 38));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
		root.RowStyles.Add(new RowStyle(SizeType.Absolute, 44));

        FlowLayoutPanel filters = new() { Dock = DockStyle.Fill, WrapContents = false };
        m_historyCategoryCombo.Items.AddRange(["전체", "장비", "소비", "재료"]);
        m_historyCategoryCombo.SelectedIndex = 0;
		m_historySortCombo.Items.AddRange(["최근 거래순", "낮은 거래가순", "높은 거래가순"]);
		m_historySortCombo.SelectedIndex = 0;
        AddFilter(filters, "카테고리", m_historyCategoryCombo, 100);
        AddFilter(filters, "아이템 이름", m_historyItemNameInput, 170);
		AddFilter(filters, "정렬", m_historySortCombo, 125);
        AddFilter(filters, "최소 STR", m_historyStrInput, 82);
        AddFilter(filters, "최소 DEX", m_historyDexInput, 82);
        AddFilter(filters, "최소 INT", m_historyIntInput, 82);
        AddFilter(filters, "최소 LUK", m_historyLukInput, 82);
        m_historySearchButton.Width = 92;
        m_historySearchButton.Height = 35;
        m_historySearchButton.Margin = new Padding(0, 27, 0, 0);
        filters.Controls.Add(m_historySearchButton);

        m_historySummaryLabel.ForeColor = s_muted;
        m_historySummaryLabel.Margin = new Padding(2, 8, 0, 0);

        SplitContainer split = new()
        {
            Dock = DockStyle.Fill,
            SplitterDistance = 800,
            FixedPanel = FixedPanel.Panel2
        };
        split.SizeChanged += (_, _) => AdjustAuctionSplit(split);
        m_saleHistoryGrid.Columns.Add("Name", "아이템 이름");
        m_saleHistoryGrid.Columns.Add("Quantity", "수량");
        m_saleHistoryGrid.Columns.Add("Stats", "능력치");
        m_saleHistoryGrid.Columns.Add("Price", "거래가");
        m_saleHistoryGrid.Columns.Add("Type", "거래 방식");
        m_saleHistoryGrid.Columns.Add("SoldAt", "거래 시각");
		DisableGridSorting(m_saleHistoryGrid);
        split.Panel1.Controls.Add(m_saleHistoryGrid);
        split.Panel2.Controls.Add(BuildSaleHistoryDetailPanel());

        root.Controls.Add(filters, 0, 0);
        root.Controls.Add(m_historySummaryLabel, 0, 1);
        root.Controls.Add(split, 0, 2);
		root.Controls.Add(BuildPaginationBar(m_historyPrevButton, m_historyPageLabel, m_historyNextButton), 0, 3);
        m_saleHistoryView.Controls.Add(root);
    }

	private static Control BuildPaginationBar(Button previousButton, Label pageLabel, Button nextButton)
	{
		TableLayoutPanel bar = new() { Dock = DockStyle.Fill, ColumnCount = 3, Padding = new Padding(0, 6, 0, 0) };
		bar.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50));
		bar.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
		bar.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50));

		FlowLayoutPanel controls = new()
		{
			AutoSize = true,
			FlowDirection = FlowDirection.LeftToRight,
			WrapContents = false,
			Margin = Padding.Empty
		};
		previousButton.Size = new Size(82, 32);
		nextButton.Size = new Size(82, 32);
		pageLabel.MinimumSize = new Size(96, 32);
		pageLabel.Padding = new Padding(0, 7, 0, 0);
		controls.Controls.Add(previousButton);
		controls.Controls.Add(pageLabel);
		controls.Controls.Add(nextButton);
		bar.Controls.Add(controls, 1, 0);
		return bar;
	}

    private Control BuildSaleHistoryDetailPanel()
    {
        TableLayoutPanel panel = CreateCardLayout();
        panel.Controls.Add(new Label
        {
            Text = "거래 상세",
            AutoSize = true,
            Font = new Font(Font, FontStyle.Bold),
            ForeColor = s_gold
        });
        panel.Controls.Add(m_historyDetailNameLabel);
        panel.Controls.Add(m_historyDetailStatsLabel);
        panel.Controls.Add(m_historyDetailPriceLabel);
        panel.Controls.Add(m_historyDetailSellerLabel);
        return panel;
    }

    private void BuildMyBidsView()
    {
        TableLayoutPanel root = new() { Dock = DockStyle.Fill, RowCount = 2 };
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 54));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        FlowLayoutPanel header = CreateSectionHeader("내 입찰 내역", m_refreshBidsButton);
        m_refundBidButton.Width = 110;
        m_refundBidButton.Height = 34;
        header.Controls.Add(m_refundBidButton);
        m_bidGrid.Columns.Add("BidId", "Bid ID");
        m_bidGrid.Columns.Add("ListingId", "매물 ID");
        m_bidGrid.Columns.Add("Amount", "입찰가");
        m_bidGrid.Columns.Add("Current", "현재가");
        m_bidGrid.Columns.Add("State", "입찰 상태");
        root.Controls.Add(header);
        root.Controls.Add(m_bidGrid);
        m_myBidsView.Controls.Add(root);
    }

    private void BuildMyListingsView()
    {
        TableLayoutPanel root = new() { Dock = DockStyle.Fill, RowCount = 2 };
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 54));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        root.Controls.Add(CreateSectionHeader("내 판매 매물", m_refreshMyListingsButton));
        m_myListingsGrid.Columns.Add("Name", "아이템 이름");
        m_myListingsGrid.Columns.Add("Quantity", "수량");
        m_myListingsGrid.Columns.Add("Stats", "능력치");
        m_myListingsGrid.Columns.Add("Current", "현재가");
        m_myListingsGrid.Columns.Add("Buyout", "즉시구매가");
        m_myListingsGrid.Columns.Add("Remaining", "남은 시간");
        SplitContainer split = new()
        {
            Dock = DockStyle.Fill,
            SplitterDistance = 780,
            FixedPanel = FixedPanel.Panel2
        };
        split.SizeChanged += (_, _) => AdjustInventorySplit(split);
        split.Panel1.Controls.Add(m_myListingsGrid);
        split.Panel2.Controls.Add(BuildMyListingDetailPanel());
        root.Controls.Add(split, 0, 1);
        m_myListingsView.Controls.Add(root);
    }

    private Control BuildMyListingDetailPanel()
    {
        TableLayoutPanel panel = CreateCardLayout();
        panel.Controls.Add(new Label
        {
            Text = "내 매물 상세",
            AutoSize = true,
            Font = new Font(Font, FontStyle.Bold),
            ForeColor = s_gold
        });
        panel.Controls.Add(m_myListingDetailNameLabel);
        panel.Controls.Add(m_myListingDetailStatsLabel);
        panel.Controls.Add(m_myListingDetailPriceLabel);
        m_myListingCancelStateLabel.ForeColor = s_muted;
        panel.Controls.Add(m_myListingCancelStateLabel);
        m_cancelListingButton.Height = 40;
        m_cancelListingButton.Dock = DockStyle.Top;
        m_cancelListingButton.Margin = new Padding(0, 12, 0, 0);
        panel.Controls.Add(m_cancelListingButton);
        return panel;
    }

    private void BuildMailView()
    {
        TableLayoutPanel root = new() { Dock = DockStyle.Fill, RowCount = 2 };
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 54));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        root.Controls.Add(CreateSectionHeader("우편함", m_refreshMailButton));
        SplitContainer split = new() { Dock = DockStyle.Fill, SplitterDistance = 650, FixedPanel = FixedPanel.Panel2 };
        split.SizeChanged += (_, _) => AdjustMailSplit(split);
        m_mailGrid.Columns.Add("Subject", "제목");
        m_mailGrid.Columns.Add("Created", "받은 시간");
        m_mailGrid.Columns.Add("State", "상태");
        split.Panel1.Controls.Add(m_mailGrid);

        TableLayoutPanel detail = new()
        {
            Dock = DockStyle.Fill,
            Padding = new Padding(22),
            BackColor = s_panelLight,
            ColumnCount = 1,
            RowCount = 5
        };
        detail.RowStyles.Add(new RowStyle(SizeType.Absolute, 34));
        detail.RowStyles.Add(new RowStyle(SizeType.Absolute, 120));
        detail.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        detail.RowStyles.Add(new RowStyle(SizeType.Absolute, 140));
        detail.RowStyles.Add(new RowStyle(SizeType.Absolute, 48));
        detail.Controls.Add(m_mailSubjectLabel, 0, 0);
        m_mailBodyTextBox.Dock = DockStyle.Fill;
        detail.Controls.Add(m_mailBodyTextBox, 0, 1);
        m_attachmentGrid.Columns.Add("Type", "종류");
        m_attachmentGrid.Columns.Add("Value", "첨부물");
        m_attachmentGrid.Columns.Add("Quantity", "수량");
        m_attachmentGrid.Columns.Add("State", "상태");
        detail.Controls.Add(m_attachmentGrid, 0, 2);
        m_attachmentDetailLabel.Margin = new Padding(0, 10, 0, 0);
        detail.Controls.Add(m_attachmentDetailLabel, 0, 3);
        m_claimButton.Dock = DockStyle.Top;
        m_claimButton.Height = 38;
        m_claimButton.Margin = new Padding(0, 5, 0, 0);
        detail.Controls.Add(m_claimButton, 0, 4);
        split.Panel2.Controls.Add(detail);
        root.Controls.Add(split, 0, 1);
        m_mailView.Controls.Add(root);
    }

    private static void AdjustMailSplit(SplitContainer split)
    {
        const int detailWidth = 520;
        const int minimumMailListWidth = 500;
        int availableWidth = split.ClientSize.Width - split.SplitterWidth;
        if (availableWidth < minimumMailListWidth + detailWidth)
        {
            return;
        }

        split.SplitterDistance = availableWidth - detailWidth;
    }

    private void BuildSellView()
    {
        TableLayoutPanel root = new() { Dock = DockStyle.Fill, RowCount = 3 };
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 54));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 92));
		FlowLayoutPanel sellHeader = CreateSectionHeader("판매할 아이템 선택", m_refreshInventoryButton);
		m_listingLimitLabel.ForeColor = s_muted;
		m_listingLimitLabel.Margin = new Padding(16, 8, 0, 0);
		sellHeader.Controls.Add(m_listingLimitLabel);
		root.Controls.Add(sellHeader);
        m_inventoryGrid.Columns.Add("Name", "아이템 이름");
        m_inventoryGrid.Columns.Add("Quantity", "수량");
        m_inventoryGrid.Columns.Add("Stats", "능력치");
        m_inventoryGrid.Columns.Add("Tradable", "거래 가능");
        SplitContainer split = new()
        {
            Dock = DockStyle.Fill,
            SplitterDistance = 780,
            FixedPanel = FixedPanel.Panel2
        };
        split.SizeChanged += (_, _) => AdjustInventorySplit(split);
        split.Panel1.Controls.Add(m_inventoryGrid);
        split.Panel2.Controls.Add(BuildInventoryDetailPanel());
        root.Controls.Add(split, 0, 1);
        FlowLayoutPanel inputs = new() { Dock = DockStyle.Fill, Padding = new Padding(0, 8, 0, 0) };
        AddFilter(inputs, "시작가", m_startPriceInput, 140);
        AddFilter(inputs, "즉시구매가", m_buyoutPriceInput, 140);
		m_startPriceInput.Minimum = 1;
		m_startPriceInput.Value = 1_000;
		m_buyoutPriceInput.Value = 5_000;
		m_durationInput.Minimum = 1;
		m_durationInput.Maximum = uint.MaxValue;
		m_durationInput.Value = 3_600;
        AddFilter(inputs, "판매 시간(초)", m_durationInput, 140);
        m_registerListingButton.Width = 130;
        m_registerListingButton.Height = 35;
        inputs.Controls.Add(m_registerListingButton);
        root.Controls.Add(inputs);
        m_sellView.Controls.Add(root);
    }

    private Control BuildInventoryDetailPanel()
    {
        TableLayoutPanel panel = CreateCardLayout();
        panel.Controls.Add(new Label
        {
            Text = "아이템 상세",
            AutoSize = true,
            Font = new Font(Font, FontStyle.Bold),
            ForeColor = s_gold
        });
        panel.Controls.Add(m_inventoryDetailNameLabel);
        panel.Controls.Add(m_inventoryDetailStatsLabel);
        panel.Controls.Add(m_inventoryDetailStateLabel);
        return panel;
    }

    private static void AdjustInventorySplit(SplitContainer split)
    {
        const int detailWidth = 320;
        const int minimumGridWidth = 500;
        int availableWidth = split.ClientSize.Width - split.SplitterWidth;
        if (availableWidth < minimumGridWidth + detailWidth)
        {
            return;
        }

        split.SplitterDistance = availableWidth - detailWidth;
    }

    private void ConfigureListingGrid()
    {
        m_listingGrid.Columns.Add("Name", "아이템");
        m_listingGrid.Columns.Add("Quantity", "수량");
        m_listingGrid.Columns.Add("Stats", "능력치");
        m_listingGrid.Columns.Add("Current", "현재가");
        m_listingGrid.Columns.Add("Buyout", "즉시구매가");
        m_listingGrid.Columns.Add("Remaining", "남은 시간");
        m_listingGrid.Columns.Add("Seller", "판매자 ID");

        SetListingColumnLayout("Name", 125, 125);
        SetListingColumnLayout("Quantity", 55, 55);
        SetListingColumnLayout("Stats", 130, 150);
        SetListingColumnLayout("Current", 80, 85);
        SetListingColumnLayout("Buyout", 90, 95);
        SetListingColumnLayout("Remaining", 100, 105);
        SetListingColumnLayout("Seller", 100, 110);
		DisableGridSorting(m_listingGrid);
    }

	private static void DisableGridSorting(DataGridView grid)
	{
		foreach (DataGridViewColumn column in grid.Columns)
			column.SortMode = DataGridViewColumnSortMode.NotSortable;
	}

    private void SetListingColumnLayout(string columnName, int minimumWidth, float fillWeight)
    {
        DataGridViewColumn column = m_listingGrid.Columns[columnName]
            ?? throw new InvalidOperationException($"Listing column not found: {columnName}");
        column.MinimumWidth = minimumWidth;
        column.FillWeight = fillWeight;
    }

    private void WireEvents()
    {
        m_loginButton.Click += async (_, _) => await LoginAsync();
        m_registerButton.Click += async (_, _) => await RegisterAsync();
        m_passwordTextBox.KeyDown += async (_, eventArgs) =>
        {
            if (eventArgs.KeyCode == Keys.Enter)
            {
                eventArgs.SuppressKeyPress = true;
                await LoginAsync();
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
        m_auctionButton.Click += async (_, _) => { ShowView(m_auctionView, m_auctionButton); await SearchAsync(); };
        m_saleHistoryButton.Click += async (_, _) => { ShowView(m_saleHistoryView, m_saleHistoryButton); await SearchSaleHistoryAsync(); };
        m_myListingsButton.Click += async (_, _) => { ShowView(m_myListingsView, m_myListingsButton); await RefreshMyListingsAsync(); };
        m_myBidsButton.Click += async (_, _) => { ShowView(m_myBidsView, m_myBidsButton); await RefreshBidsAsync(); };
        m_mailButton.Click += async (_, _) => { ShowView(m_mailView, m_mailButton); await RefreshMailAsync(); };
        m_sellButton.Click += async (_, _) => { ShowView(m_sellView, m_sellButton); await RefreshInventoryAsync(); };
        m_searchButton.Click += async (_, _) => await SearchAsync();
        m_historySearchButton.Click += async (_, _) => await SearchSaleHistoryAsync();
		m_listingPrevButton.Click += async (_, _) => await MoveListingPageAsync(-1);
		m_listingNextButton.Click += async (_, _) => await MoveListingPageAsync(1);
		m_historyPrevButton.Click += async (_, _) => await MoveHistoryPageAsync(-1);
		m_historyNextButton.Click += async (_, _) => await MoveHistoryPageAsync(1);
		m_itemNameInput.KeyDown += async (_, eventArgs) =>
		{
			if (eventArgs.KeyCode == Keys.Enter)
			{
				eventArgs.SuppressKeyPress = true;
				await SearchAsync();
			}
		};
        m_historyItemNameInput.KeyDown += async (_, eventArgs) =>
        {
            if (eventArgs.KeyCode == Keys.Enter)
            {
                eventArgs.SuppressKeyPress = true;
                await SearchSaleHistoryAsync();
            }
        };
		m_categoryCombo.SelectedIndexChanged += (_, _) => InvalidateListingPaging();
		m_itemNameInput.TextChanged += (_, _) => InvalidateListingPaging();
		m_listingSortCombo.SelectedIndexChanged += (_, _) => InvalidateListingPaging();
		m_strInput.ValueChanged += (_, _) => InvalidateListingPaging();
		m_dexInput.ValueChanged += (_, _) => InvalidateListingPaging();
		m_intInput.ValueChanged += (_, _) => InvalidateListingPaging();
		m_lukInput.ValueChanged += (_, _) => InvalidateListingPaging();
		m_historyCategoryCombo.SelectedIndexChanged += (_, _) => InvalidateHistoryPaging();
		m_historyItemNameInput.TextChanged += (_, _) => InvalidateHistoryPaging();
		m_historySortCombo.SelectedIndexChanged += (_, _) => InvalidateHistoryPaging();
		m_historyStrInput.ValueChanged += (_, _) => InvalidateHistoryPaging();
		m_historyDexInput.ValueChanged += (_, _) => InvalidateHistoryPaging();
		m_historyIntInput.ValueChanged += (_, _) => InvalidateHistoryPaging();
		m_historyLukInput.ValueChanged += (_, _) => InvalidateHistoryPaging();
        m_refreshMyListingsButton.Click += async (_, _) => await RefreshMyListingsAsync();
        m_cancelListingButton.Click += async (_, _) => await CancelSelectedListingAsync();
        m_refreshBidsButton.Click += async (_, _) => await RefreshBidsAsync();
        m_refundBidButton.Click += async (_, _) => await RefundSelectedBidAsync();
		m_bidGrid.SelectionChanged += (_, _) =>
		{
			m_refundBidButton.Enabled = m_bidGrid.CurrentRow?.Tag is BidSummary { BidState: 3 };
		};
        m_refreshMailButton.Click += async (_, _) => await RefreshMailAsync();
        m_refreshInventoryButton.Click += async (_, _) => await RefreshInventoryAsync();
        m_listingGrid.SelectionChanged += async (_, _) =>
        {
            if (!m_isUpdatingListingGrid) await LoadSelectedListingAsync();
        };
        m_myListingsGrid.SelectionChanged += async (_, _) =>
        {
            if (!m_isUpdatingMyListingsGrid) await LoadSelectedMyListingAsync();
        };
        m_saleHistoryGrid.SelectionChanged += async (_, _) =>
        {
            if (!m_isUpdatingSaleHistoryGrid) await LoadSelectedSaleHistoryAsync();
        };
        m_mailGrid.SelectionChanged += async (_, _) =>
        {
            if (!m_isUpdatingMailGrid) await LoadSelectedMailAsync();
        };
        m_attachmentGrid.SelectionChanged += (_, _) =>
        {
            if (!m_isUpdatingAttachmentGrid) ShowSelectedAttachmentDetail();
        };
        m_inventoryGrid.SelectionChanged += (_, _) =>
        {
            if (!m_isUpdatingInventoryGrid) ShowSelectedInventoryDetail();
        };
        m_bidButton.Click += async (_, _) => await BidAsync();
        m_buyoutButton.Click += async (_, _) => await BuyoutAsync();
        m_claimButton.Click += async (_, _) => await ClaimAttachmentAsync();
        m_registerListingButton.Click += async (_, _) => await RegisterListingAsync();
        m_cheatButton.Click += async (_, _) => await ExecuteCheatAsync();
        m_cheatInput.KeyDown += async (_, eventArgs) =>
        {
            if (eventArgs.KeyCode == Keys.Enter)
            {
                eventArgs.SuppressKeyPress = true;
                await ExecuteCheatAsync();
            }
        };
        m_toastTimer.Tick += (_, _) => { m_toastTimer.Stop(); m_toastLabel.Visible = false; };
        m_client.ConnectionStateChanged += connected => Ui(() =>
        {
            m_connectionLabel.Text = connected ? "● 서버 연결됨" : "● 연결 끊김";
            m_connectionLabel.ForeColor = connected ? Color.FromArgb(76, 175, 80) : Color.FromArgb(239, 83, 80);
        });
        m_client.SystemMessageReceived += message => Ui(() => SetStatus(message));
        m_client.OutbidReceived += notification => Ui(() =>
        {
            m_myBidsButton.HasNotification = true;
            ShowToast($"상위 입찰자가 변경되었습니다  ·  매물 {notification.ListingId:N0}");
        });
        m_client.AuctionWonReceived += notification => Ui(() =>
        {
            m_mailButton.HasNotification = true;
            ShowToast($"경매에 낙찰되었습니다  ·  우편 {notification.ItemMailId:N0}");
        });
    }

    private async Task LoginAsync()
    {
        SetAuthControlsEnabled(false);
        try
        {
            await RunUiOperationAsync(async () =>
            {
                m_loginStatusLabel.Text = "● 로그인 중...";
                LoginAccountResponse login = await m_authApiClient.LoginAsync(
                    new AuthServerSettings(m_settings.LoginServerBaseUrl),
                    new LoginAccountRequest(m_loginIdTextBox.Text.Trim(), m_passwordTextBox.Text));
                await m_client.ConnectAsync(new(
                    login.AuctionServer.Ip,
                    login.AuctionServer.Port,
                    m_settings.AuctionPacketKey));
                AuctionAuthResult auth = await m_client.AuthenticateAsync(login.AuctionTicket);
                if (auth.ResultCode != 0)
                {
                    if (auth.ResultCode == 8 && auth.UserId == 0)
                    {
                        throw new InvalidOperationException(
                            "AuctionServer가 Redis 인증 모드로 실행되지 않았습니다. " +
                            "Config/Server/AuctionHouseServer.yaml의 Authentication.Enabled를 true로 설정해 주세요.");
                    }
                    throw new InvalidOperationException($"AuctionAuth 실패: {GetResultText(auth.ResultCode)}");
                }

                m_userId = auth.UserId;
				ApplyAuctionPolicy(auth);
                m_nickname = login.Nickname;
                m_userLabel.Text = $"{m_nickname}  ·  User {m_userId}";
                m_loginPanel.Visible = false;
                m_mainPanel.Visible = true;
                ShowView(m_auctionView, m_auctionButton);
                await SearchAsync();
            }, "로그인 완료");
        }
        finally
        {
            SetAuthControlsEnabled(true);
        }
    }

    private async Task RegisterAsync()
    {
        try
        {
            SetAuthControlsEnabled(false);
            m_loginStatusLabel.Text = "● 회원가입 처리 중...";
            RegisterAccountRequest request = new(
                m_registerIdTextBox.Text.Trim(),
                m_registerPasswordTextBox.Text,
                m_registerNicknameTextBox.Text.Trim());
            RegisterAccountResponse response = await m_authApiClient.RegisterAsync(
                new AuthServerSettings(m_settings.LoginServerBaseUrl),
                request);

            m_loginIdTextBox.Text = request.LoginId;
            m_passwordTextBox.Text = request.Password;
            m_registerPasswordTextBox.Clear();
            m_authTabControl.SelectedIndex = 0;
            m_loginStatusLabel.Text = $"● 회원가입 완료 · User {response.UserId}";
            MessageBox.Show(
                this,
                $"회원가입이 완료되었습니다.\r\n아이디: {request.LoginId}\r\n닉네임: {response.Nickname}",
                "회원가입 성공",
                MessageBoxButtons.OK,
                MessageBoxIcon.Information);
        }
        catch (Exception exception)
        {
            m_loginStatusLabel.Text = $"● 회원가입 실패 · {exception.Message}";
            MessageBox.Show(this, exception.Message, "회원가입 실패", MessageBoxButtons.OK, MessageBoxIcon.Warning);
        }
        finally
        {
            SetAuthControlsEnabled(true);
        }
    }

    private void SetAuthControlsEnabled(bool enabled)
    {
        m_authTabControl.Enabled = enabled;
        m_loginButton.Enabled = enabled;
        m_registerButton.Enabled = enabled;
        UseWaitCursor = !enabled;
    }

	private void ResetListingPaging()
	{
		m_listingPageCursors.Clear();
		m_listingPageCursors.Add((0, 0));
		m_listingPageIndex = 0;
		m_listingHasNextPage = false;
		UpdateListingPagingControls();
	}

	private void InvalidateListingPaging()
	{
		ResetListingPaging();
		m_listingNextButton.Enabled = false;
	}

	private void UpdateListingPagingControls()
	{
		m_listingPageLabel.Text = $"페이지 {m_listingPageIndex + 1:N0}";
		m_listingPrevButton.Enabled = m_listingPageIndex > 0;
		m_listingNextButton.Enabled = m_listingHasNextPage;
	}

	private async Task MoveListingPageAsync(int direction)
	{
		if (direction < 0)
		{
			if (m_listingPageIndex == 0)
				return;
			--m_listingPageIndex;
			await SearchAsync(resetPaging: false);
			return;
		}

		if (!m_listingHasNextPage || m_listingGrid.Rows.Cast<DataGridViewRow>().LastOrDefault()?.Tag is not ListingSummary last)
			return;

		if (m_listingPageCursors.Count > m_listingPageIndex + 1)
			m_listingPageCursors.RemoveRange(m_listingPageIndex + 1, m_listingPageCursors.Count - m_listingPageIndex - 1);
		m_listingPageCursors.Add((GetListingCursorSortValue(last), last.ListingId));
		++m_listingPageIndex;
		await SearchAsync(resetPaging: false);
	}

    private async Task SearchAsync(bool resetPaging = true)
    {
		if (resetPaging)
			ResetListingPaging();

        await RunUiOperationAsync(async () =>
        {
            string searchText = m_itemNameInput.Text.Trim();
            IReadOnlyList<uint> itemDataIds = ItemCatalog.FindIds(searchText);
            if (searchText.Length != 0 && itemDataIds.Count == 0)
            {
                m_isUpdatingListingGrid = true;
                m_listingGrid.Rows.Clear();
                m_isUpdatingListingGrid = false;
                m_selectedListing = null;
                ClearListingDetail();
				m_listingHasNextPage = false;
				UpdateListingPagingControls();
                SetStatus($"'{searchText}'에 해당하는 아이템 기획 데이터가 없습니다.");
                return;
            }
			(ulong cursorSortValue, ulong cursorListingId) = m_listingPageCursors[m_listingPageIndex];
			var result = await m_client.SearchListingsAsync(
                (byte)m_categoryCombo.SelectedIndex,
                itemDataIds,
                (uint)m_strInput.Value,
                (uint)m_dexInput.Value,
                (uint)m_intInput.Value,
				(uint)m_lukInput.Value,
				sellerOnly: false,
				sortType: (byte)(m_listingSortCombo.SelectedIndex + 1),
				cursorSortValue: cursorSortValue,
				cursorListingId: cursorListingId,
				limit: checked((uint)(m_searchPageSize + 1)));
            EnsureSuccess(result.ResultCode, "매물 조회");
			IReadOnlyList<ListingSummary> visibleListings = result.Listings.Take(m_searchPageSize).ToArray();
			m_listingHasNextPage = result.Listings.Count > m_searchPageSize;
            m_isUpdatingListingGrid = true;
            try
            {
                m_listingGrid.Rows.Clear();
				foreach (ListingSummary listing in visibleListings)
                {
                    int rowIndex = m_listingGrid.Rows.Add(
                        listing.Name,
                        listing.Quantity,
                        FormatStats(listing),
                        GetDisplayedCurrentPrice(listing.StartPrice, listing.CurrentBidPrice).ToString("N0"),
                        listing.BuyoutPrice.ToString("N0"),
                        FormatRemaining(listing.ExpiresAtUnixMs),
                        listing.SellerLoginId);
                    m_listingGrid.Rows[rowIndex].Tag = listing;
                }
                SelectFirstRow(m_listingGrid);
            }
            finally
            {
                m_isUpdatingListingGrid = false;
            }

            if (m_listingGrid.Rows.Count > 0)
            {
                await LoadSelectedListingAsync();
            }
            else
            {
                m_selectedListing = null;
                ClearListingDetail();
            }
			UpdateListingPagingControls();
		}, "매물 목록을 갱신했습니다.");
	}

	private ulong GetListingCursorSortValue(ListingSummary listing) =>
		(SearchSortType)(m_listingSortCombo.SelectedIndex + 1) switch
		{
			SearchSortType.PriceAscending or SearchSortType.PriceDescending =>
				GetDisplayedCurrentPrice(listing.StartPrice, listing.CurrentBidPrice),
			SearchSortType.ExpiringSoon => listing.ExpiresAtUnixMs,
			_ => listing.ListingId
		};

	private void ResetHistoryPaging()
	{
		m_historyPageCursors.Clear();
		m_historyPageCursors.Add((0, 0));
		m_historyPageIndex = 0;
		m_historyHasNextPage = false;
		UpdateHistoryPagingControls();
	}

	private void InvalidateHistoryPaging()
	{
		ResetHistoryPaging();
		m_historyNextButton.Enabled = false;
	}

	private void UpdateHistoryPagingControls()
	{
		m_historyPageLabel.Text = $"페이지 {m_historyPageIndex + 1:N0}";
		m_historyPrevButton.Enabled = m_historyPageIndex > 0;
		m_historyNextButton.Enabled = m_historyHasNextPage;
	}

	private async Task MoveHistoryPageAsync(int direction)
	{
		if (direction < 0)
		{
			if (m_historyPageIndex == 0)
				return;
			--m_historyPageIndex;
			await SearchSaleHistoryAsync(resetPaging: false);
			return;
		}

		if (!m_historyHasNextPage || m_saleHistoryGrid.Rows.Cast<DataGridViewRow>().LastOrDefault()?.Tag is not SaleHistorySummary last)
			return;

		if (m_historyPageCursors.Count > m_historyPageIndex + 1)
			m_historyPageCursors.RemoveRange(m_historyPageIndex + 1, m_historyPageCursors.Count - m_historyPageIndex - 1);
		m_historyPageCursors.Add((GetHistoryCursorSortValue(last), last.ListingId));
		++m_historyPageIndex;
		await SearchSaleHistoryAsync(resetPaging: false);
	}

    private async Task SearchSaleHistoryAsync(bool resetPaging = true)
    {
		if (resetPaging)
			ResetHistoryPaging();

        await RunUiOperationAsync(async () =>
        {
            string searchText = m_historyItemNameInput.Text.Trim();
            IReadOnlyList<uint> itemDataIds = ItemCatalog.FindIds(searchText);
            if (searchText.Length != 0 && itemDataIds.Count == 0)
            {
                m_isUpdatingSaleHistoryGrid = true;
                m_saleHistoryGrid.Rows.Clear();
                m_isUpdatingSaleHistoryGrid = false;
                m_historySummaryLabel.Text = $"'{searchText}'에 해당하는 아이템 기획 데이터가 없습니다.";
                ClearSaleHistoryDetail();
				m_historyHasNextPage = false;
				UpdateHistoryPagingControls();
                return;
            }

			(ulong cursorSortValue, ulong cursorListingId) = m_historyPageCursors[m_historyPageIndex];
            var result = await m_client.SearchSaleHistoryAsync(
                (byte)m_historyCategoryCombo.SelectedIndex,
                itemDataIds,
                (uint)m_historyStrInput.Value,
                (uint)m_historyDexInput.Value,
				(uint)m_historyIntInput.Value,
				(uint)m_historyLukInput.Value,
				sortType: (byte)(m_historySortCombo.SelectedIndex + 1),
				cursorSortValue: cursorSortValue,
				cursorListingId: cursorListingId,
				limit: checked((uint)(m_searchPageSize + 1)));
            EnsureSuccess(result.ResultCode, "판매 이력 조회");
			IReadOnlyList<SaleHistorySummary> visibleHistory = result.History.Take(m_searchPageSize).ToArray();
			m_historyHasNextPage = result.History.Count > m_searchPageSize;

            m_isUpdatingSaleHistoryGrid = true;
            try
            {
                m_saleHistoryGrid.Rows.Clear();
				foreach (SaleHistorySummary sale in visibleHistory)
                {
                    int rowIndex = m_saleHistoryGrid.Rows.Add(
                        sale.Name,
                        sale.Quantity,
                        FormatStats(sale),
                        sale.FinalPrice.ToString("N0"),
                        GetSaleTypeText(sale.SaleType),
                        FormatDate(sale.SoldAtUnixMs));
                    m_saleHistoryGrid.Rows[rowIndex].Tag = sale;
                }
                SelectFirstRow(m_saleHistoryGrid);
            }
            finally
            {
                m_isUpdatingSaleHistoryGrid = false;
            }

			UpdateHistoryPagingControls();
			if (visibleHistory.Count == 0)
            {
                m_historySummaryLabel.Text = "조건에 맞는 판매 완료 이력이 없습니다.";
                ClearSaleHistoryDetail();
                return;
            }

			ulong minimum = visibleHistory.Min(sale => sale.FinalPrice);
			ulong maximum = visibleHistory.Max(sale => sale.FinalPrice);
			decimal average = visibleHistory.Average(sale => (decimal)sale.FinalPrice);
            m_historySummaryLabel.Text =
				$"페이지 {m_historyPageIndex + 1:N0} · {visibleHistory.Count:N0}건  ·  최저 {minimum:N0}  ·  평균 {average:N0}  ·  최고 {maximum:N0} {GetCurrencyName(m_defaultCurrencyId)}";
            await LoadSelectedSaleHistoryAsync();
		}, "판매 이력을 갱신했습니다.");
	}

	private ulong GetHistoryCursorSortValue(SaleHistorySummary history) =>
		(SearchSortType)(m_historySortCombo.SelectedIndex + 1) switch
		{
			SearchSortType.PriceAscending or SearchSortType.PriceDescending => history.FinalPrice,
			_ => history.SoldAtUnixMs
		};

    private async Task LoadSelectedSaleHistoryAsync()
    {
        if (m_saleHistoryGrid.CurrentRow?.Tag is not SaleHistorySummary summary)
        {
            ClearSaleHistoryDetail();
            return;
        }

        await RunUiOperationAsync(async () =>
        {
            var result = await m_client.GetSaleHistoryDetailAsync(summary.ListingId);
            EnsureSuccess(result.ResultCode, "판매 이력 상세 조회");
            SaleHistoryDetail detail = result.Detail ?? throw new InvalidDataException("판매 이력 상세가 비어 있습니다.");
            m_historyDetailNameLabel.Text = $"{detail.Name} × {detail.Quantity:N0}";
            m_historyDetailStatsLabel.Text =
                $"분류  {ItemCatalog.GetCategoryName(detail.ItemDataId)}\n" +
                $"STR  {detail.Strength:N0}\nDEX  {detail.Dexterity:N0}\n" +
                $"INT  {detail.Intelligence:N0}\nLUK  {detail.Luck:N0}";
            m_historyDetailPriceLabel.Text =
                $"등록 시작가  {detail.StartPrice:N0}\n최종 거래가  {detail.FinalPrice:N0}\n" +
                $"거래 방식  {GetSaleTypeText(detail.SaleType)}\n거래 시각  {FormatDate(detail.SoldAtUnixMs)}";
            m_historyDetailSellerLabel.Text = $"판매자  {detail.SellerLoginId}";
        }, "판매 이력 상세를 불러왔습니다.", showErrorOnly: true);
    }

    private void ClearSaleHistoryDetail()
    {
        m_historyDetailNameLabel.Text = "거래 이력을 선택하세요";
        m_historyDetailStatsLabel.Text = string.Empty;
        m_historyDetailPriceLabel.Text = string.Empty;
        m_historyDetailSellerLabel.Text = string.Empty;
    }

    private async Task RefreshMyListingsAsync()
    {
        await RunUiOperationAsync(async () =>
        {
            var result = await m_client.SearchListingsAsync(
                0, Array.Empty<uint>(), 0, 0, 0, 0, sellerOnly: true);
            EnsureSuccess(result.ResultCode, "내 판매 매물 조회");
			UpdateListingLimit(result.Listings.Count);
            m_isUpdatingMyListingsGrid = true;
            try
            {
                m_myListingsGrid.Rows.Clear();
                foreach (ListingSummary listing in result.Listings)
                {
                    int rowIndex = m_myListingsGrid.Rows.Add(
                        listing.Name,
                        listing.Quantity,
                        FormatStats(listing),
                        GetDisplayedCurrentPrice(listing.StartPrice, listing.CurrentBidPrice).ToString("N0"),
                        listing.BuyoutPrice.ToString("N0"),
                        FormatRemaining(listing.ExpiresAtUnixMs));
                    m_myListingsGrid.Rows[rowIndex].Tag = listing;
                }
                SelectFirstRow(m_myListingsGrid);
            }
            finally
            {
                m_isUpdatingMyListingsGrid = false;
            }
            if (m_myListingsGrid.Rows.Count > 0)
            {
                await LoadSelectedMyListingAsync();
            }
            else
            {
                ClearMyListingDetail();
            }
            m_myListingsButton.HasNotification = false;
        }, "내 판매 매물을 갱신했습니다.");
    }

    private async Task LoadSelectedMyListingAsync()
    {
        if (m_myListingsGrid.CurrentRow?.Tag is not ListingSummary summary)
        {
            ClearMyListingDetail();
            return;
        }

        await RunUiOperationAsync(async () =>
        {
            var result = await m_client.GetListingDetailAsync(summary.ListingId);
            EnsureSuccess(result.ResultCode, "내 매물 상세 조회");
            ListingDetail detail = result.Detail ?? throw new InvalidDataException("내 매물 상세가 비어 있습니다.");
            if (detail.SellerUserId != m_userId)
            {
                throw new InvalidDataException("본인이 등록한 매물이 아닙니다.");
            }

            m_selectedMyListing = detail;
            m_myListingDetailNameLabel.Text = $"{detail.Name} × {detail.Quantity:N0}";
            m_myListingDetailStatsLabel.Text =
                $"분류  {ItemCatalog.GetCategoryName(detail.ItemDataId)}\n" +
                $"STR  {detail.Strength:N0}\nDEX  {detail.Dexterity:N0}\n" +
                $"INT  {detail.Intelligence:N0}\nLUK  {detail.Luck:N0}";
            ulong currentPrice = GetDisplayedCurrentPrice(detail.StartPrice, detail.CurrentBidPrice);
            m_myListingDetailPriceLabel.Text =
                $"시작가  {detail.StartPrice:N0}\n현재가  {currentPrice:N0}\n" +
                $"즉시 구매가  {detail.BuyoutPrice:N0}\n남은 시간  {FormatRemaining(detail.ExpiresAtUnixMs)}";
            bool canCancel = detail.HighestBidderUserId == 0;
            m_myListingCancelStateLabel.Text = canCancel
                ? "입찰자가 없어 판매를 취소할 수 있습니다.\n취소한 아이템은 우편으로 반환됩니다."
                : "입찰자가 존재하여 판매를 취소할 수 없습니다.";
            m_cancelListingButton.Enabled = canCancel;
        }, "내 매물 상세를 불러왔습니다.", showErrorOnly: true);
    }

    private async Task CancelSelectedListingAsync()
    {
        ListingDetail? listing = m_selectedMyListing;
        if (listing is null || listing.HighestBidderUserId != 0)
        {
            return;
        }

        DialogResult confirmation = MessageBox.Show(
            this,
            $"{listing.Name} 판매를 취소하시겠습니까?\n아이템은 우편함으로 반환됩니다.",
            "판매 취소",
            MessageBoxButtons.YesNo,
            MessageBoxIcon.Question,
            MessageBoxDefaultButton.Button2);
        if (confirmation != DialogResult.Yes)
        {
            return;
        }

        await RunUiOperationAsync(async () =>
        {
            ListingCancelResult result = await m_client.CancelListingAsync(listing.ListingId, listing.Version);
            EnsureSuccess(result.ResultCode, "판매 취소");
            m_selectedMyListing = null;
            m_mailButton.HasNotification = true;
            ShowToast($"판매를 취소했습니다 · 반환 우편 {result.ReturnMailId:N0}");
            await RefreshMyListingsAsync();
        }, "판매 취소 완료");
    }

    private void ClearMyListingDetail()
    {
        m_selectedMyListing = null;
        m_myListingDetailNameLabel.Text = "매물을 선택하세요";
        m_myListingDetailStatsLabel.Text = string.Empty;
        m_myListingDetailPriceLabel.Text = string.Empty;
        m_myListingCancelStateLabel.Text = string.Empty;
        m_cancelListingButton.Enabled = false;
    }

    private async Task ExecuteCheatAsync()
    {
        string[] tokens = m_cheatInput.Text.Split(' ', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
        if (tokens.Length == 0)
        {
            return;
        }

        byte cheatType;
        ulong amount = 0;
        uint itemDataId = 0;
        uint strength = 0;
        uint dexterity = 0;
        uint intelligence = 0;
        uint luck = 0;
        if (tokens[0].Equals("/gold", StringComparison.OrdinalIgnoreCase) && tokens.Length == 2 &&
            ulong.TryParse(tokens[1], out amount) && amount > 0)
        {
            cheatType = 1;
        }
        else if (tokens[0].Equals("/item", StringComparison.OrdinalIgnoreCase) && tokens.Length == 6 &&
                 uint.TryParse(tokens[1], out itemDataId) && uint.TryParse(tokens[2], out strength) &&
                 uint.TryParse(tokens[3], out dexterity) && uint.TryParse(tokens[4], out intelligence) &&
                 uint.TryParse(tokens[5], out luck))
        {
            cheatType = 2;
        }
        else
        {
            MessageBox.Show(this,
                "사용법\r\n/gold n\r\n/item item_data_id str dex int luk",
                "치트 명령 형식 오류",
                MessageBoxButtons.OK,
                MessageBoxIcon.Information);
            return;
        }

        await RunUiOperationAsync(async () =>
        {
            DebugCheatResult result = await m_client.ExecuteDebugCheatAsync(
                cheatType, amount, itemDataId, strength, dexterity, intelligence, luck);
            EnsureSuccess(result.ResultCode, "치트 실행");
            if (cheatType == 1)
            {
                m_balanceLabel.Text = $"{GetCurrencyName(m_defaultCurrencyId)} {result.CurrencyBalance:N0}";
            }
            else
            {
                await RefreshInventoryAsync();
            }
            m_cheatInput.Clear();
            ShowToast(result.Message);
        }, "치트를 실행했습니다.");
    }

    private async Task LoadSelectedListingAsync()
    {
        if (m_listingGrid.CurrentRow?.Tag is not ListingSummary summary)
        {
            return;
        }
        await RunUiOperationAsync(async () =>
        {
            var result = await m_client.GetListingDetailAsync(summary.ListingId);
            EnsureSuccess(result.ResultCode, "매물 상세 조회");
            m_selectedListing = result.Detail ?? throw new InvalidDataException("매물 상세가 비어 있습니다.");
            m_detailNameLabel.Text = $"{m_selectedListing.Name}  × {m_selectedListing.Quantity}";
            m_detailStatsLabel.Text = $"장비 능력치\r\nSTR  +{m_selectedListing.Strength}\r\nDEX  +{m_selectedListing.Dexterity}\r\nINT  +{m_selectedListing.Intelligence}\r\nLUK  +{m_selectedListing.Luck}";
			ulong displayedCurrentPrice = GetDisplayedCurrentPrice(m_selectedListing.StartPrice, m_selectedListing.CurrentBidPrice);
            m_detailPriceLabel.Text = $"현재가  {displayedCurrentPrice:N0}\r\n즉시 구매가  {m_selectedListing.BuyoutPrice:N0}\r\n남은 시간  {FormatRemaining(m_selectedListing.ExpiresAtUnixMs)}";
			ConfigureBidInput(m_selectedListing);
        }, "매물 상세를 불러왔습니다.", showErrorOnly: true);
    }

    private void ClearListingDetail()
    {
        m_detailNameLabel.Text = "매물을 선택하세요";
        m_detailStatsLabel.Text = string.Empty;
        m_detailPriceLabel.Text = string.Empty;
    }

    private async Task BidAsync()
    {
        if (m_selectedListing is null) return;
        await RunUiOperationAsync(async () =>
        {
            BidResult result = await m_client.BidAsync(m_selectedListing.ListingId, (ulong)m_bidAmountInput.Value, m_selectedListing.Version);
            EnsureSuccess(result.ResultCode, "입찰");
            m_balanceLabel.Text = $"{GetCurrencyName(m_selectedListing.CurrencyId)} {result.CurrencyBalance:N0}";
            ShowToast($"입찰에 성공했습니다  ·  {result.BidAmount:N0} {GetCurrencyName(m_selectedListing.CurrencyId)}");
            await LoadSelectedListingAsync();
        }, "입찰 완료");
    }

    private async Task BuyoutAsync()
    {
        if (m_selectedListing is null) return;
        await RunUiOperationAsync(async () =>
        {
            BuyoutResult result = await m_client.BuyoutAsync(m_selectedListing.ListingId, m_selectedListing.Version);
            EnsureSuccess(result.ResultCode, "즉시 구매");
            m_balanceLabel.Text = $"{GetCurrencyName(m_selectedListing.CurrencyId)} {result.CurrencyBalance:N0}";
            m_mailButton.HasNotification = true;
            ShowToast($"즉시 구매 완료  ·  우편 {result.ItemMailId:N0} 도착");
            await SearchAsync();
        }, "즉시 구매 완료");
    }

    private async Task RefreshBidsAsync()
    {
        await RunUiOperationAsync(async () =>
        {
            var result = await m_client.GetMyBidsAsync();
            EnsureSuccess(result.ResultCode, "내 입찰 조회");
            m_bidGrid.Rows.Clear();
            foreach (BidSummary bid in result.Bids)
            {
                int rowIndex = m_bidGrid.Rows.Add(bid.BidId, bid.ListingId, bid.BidAmount.ToString("N0"),
                    bid.CurrentBidPrice.ToString("N0"), GetBidStateText(bid.BidState));
                m_bidGrid.Rows[rowIndex].Tag = bid;
            }
            m_myBidsButton.HasNotification = false;
        }, "내 입찰 내역을 갱신했습니다.");
    }

    private async Task RefundSelectedBidAsync()
    {
        if (m_bidGrid.CurrentRow?.Tag is not BidSummary bid)
        {
            return;
        }
        await RunUiOperationAsync(async () =>
        {
            BidRefundResult result = await m_client.RefundBidAsync(bid);
            EnsureSuccess(result.ResultCode, "입찰 재화 회수");
            m_balanceLabel.Text = $"{GetCurrencyName(bid.CurrencyId)} {result.CurrencyBalance:N0}";
            ShowToast($"입찰 재화 {result.RefundedAmount:N0} {GetCurrencyName(bid.CurrencyId)}를 회수했습니다.");
            await RefreshBidsAsync();
        }, "재화 회수 완료");
    }

    private async Task RefreshMailAsync()
    {
        await RunUiOperationAsync(async () =>
        {
            ulong selectedMailId = m_selectedMail?.MailId ?? 0;
            var result = await m_client.GetMailsAsync();
            EnsureSuccess(result.ResultCode, "우편 목록 조회");
            m_isUpdatingMailGrid = true;
            try
            {
                m_mailGrid.Rows.Clear();
                foreach (MailSummary mail in result.Mails)
                {
                    int rowIndex = m_mailGrid.Rows.Add(mail.Subject, FormatDate(mail.CreatedAtUnixMs), GetMailStateText(mail.State));
                    m_mailGrid.Rows[rowIndex].Tag = mail;
                }
                DataGridViewRow? selectedRow = m_mailGrid.Rows
                    .Cast<DataGridViewRow>()
                    .FirstOrDefault(row => row.Tag is MailSummary candidate && candidate.MailId == selectedMailId);
                SelectRow(m_mailGrid, selectedRow ?? m_mailGrid.Rows.Cast<DataGridViewRow>().FirstOrDefault());
            }
            finally
            {
                m_isUpdatingMailGrid = false;
            }
            m_mailButton.HasNotification = false;
            if (m_mailGrid.Rows.Count > 0)
            {
                await LoadSelectedMailAsync();
            }
            else
            {
                ClearMailDetail();
            }
        }, "우편함을 갱신했습니다.");
    }

    private async Task LoadSelectedMailAsync()
    {
        if (m_mailGrid.CurrentRow?.Tag is not MailSummary mail) return;
        await RunUiOperationAsync(async () =>
        {
            var result = await m_client.GetMailDetailAsync(mail.MailId);
            EnsureSuccess(result.ResultCode, "우편 상세 조회");
            m_selectedMail = result.Detail ?? throw new InvalidDataException("우편 상세가 비어 있습니다.");
            m_mailSubjectLabel.Text = m_selectedMail.Subject;
            m_mailBodyTextBox.Text = m_selectedMail.Body;
            m_isUpdatingAttachmentGrid = true;
            try
            {
                m_attachmentGrid.Rows.Clear();
                m_attachmentDetailLabel.Text = "첨부물을 선택하세요";
                m_claimButton.Enabled = false;
                foreach (MailAttachment attachment in m_selectedMail.Attachments)
                {
                    string type = attachment.AttachmentType == 1 ? "아이템" : "재화";
                    string value = attachment.AttachmentType == 1
                        ? ItemCatalog.GetName(attachment.ItemDataId)
                        : GetCurrencyName(attachment.CurrencyId);
                    string quantity = attachment.AttachmentType == 1
                        ? attachment.Quantity.ToString("N0")
                        : attachment.CurrencyAmount.ToString("N0");
                    int rowIndex = m_attachmentGrid.Rows.Add(
						type,
						value,
						quantity,
						GetMailAttachmentStateText(attachment.State));
                    m_attachmentGrid.Rows[rowIndex].Tag = attachment;
                }
                SelectFirstRow(m_attachmentGrid);
            }
            finally
            {
                m_isUpdatingAttachmentGrid = false;
            }
            ShowSelectedAttachmentDetail();
        }, "우편 상세를 불러왔습니다.", showErrorOnly: true);
    }

    private async Task ClaimAttachmentAsync()
    {
        if (m_selectedMail is null || m_attachmentGrid.CurrentRow?.Tag is not MailAttachment attachment) return;
        await RunUiOperationAsync(async () =>
        {
            MailClaimResult result = await m_client.ClaimMailAsync(m_selectedMail.MailId, attachment.AttachmentId);
            EnsureSuccess(result.ResultCode, "첨부물 수령");
            if (result.AttachmentType == 2)
                m_balanceLabel.Text = $"{GetCurrencyName(result.CurrencyId)} {result.CurrencyBalance:N0}";
            ShowToast("첨부물을 수령했습니다.");
			await RefreshMailAsync();
        }, "첨부물 수령 완료");
    }

    private async Task RefreshInventoryAsync()
    {
        await RunUiOperationAsync(async () =>
        {
			var listingsResult = await m_client.SearchListingsAsync(
				0, Array.Empty<uint>(), 0, 0, 0, 0, sellerOnly: true, limit: m_maxActiveListings + 1);
			EnsureSuccess(listingsResult.ResultCode, "판매 등록 한도 조회");
			UpdateListingLimit(listingsResult.Listings.Count);

            var result = await m_client.GetInventoryAsync();
            EnsureSuccess(result.ResultCode, "인벤토리 조회");
            ulong selectedItemInstanceId = m_inventoryGrid.CurrentRow?.Tag is InventoryItem selectedItem
                ? selectedItem.ItemInstanceId
                : 0;
            m_isUpdatingInventoryGrid = true;
            try
            {
                m_inventoryGrid.Rows.Clear();
                foreach (InventoryItem item in result.Items)
                {
                    int rowIndex = m_inventoryGrid.Rows.Add(
                        ItemCatalog.GetName(item.ItemDataId),
                        item.Quantity,
                        FormatItemStatsInline(item.ItemData),
                        item.IsTradable ? "가능" : "불가");
                    m_inventoryGrid.Rows[rowIndex].Tag = item;
                }
                DataGridViewRow? selectedRow = m_inventoryGrid.Rows
                    .Cast<DataGridViewRow>()
                    .FirstOrDefault(row => row.Tag is InventoryItem candidate && candidate.ItemInstanceId == selectedItemInstanceId);
                SelectRow(m_inventoryGrid, selectedRow ?? m_inventoryGrid.Rows.Cast<DataGridViewRow>().FirstOrDefault());
            }
            finally
            {
                m_isUpdatingInventoryGrid = false;
            }
            if (result.Items.Count == 0)
            {
                ClearInventoryDetail();
            }
            else
            {
                ShowSelectedInventoryDetail();
            }
        }, "인벤토리를 갱신했습니다.");
    }

    private void ShowSelectedAttachmentDetail()
    {
        if (m_attachmentGrid.CurrentRow?.Tag is not MailAttachment attachment)
        {
            m_attachmentDetailLabel.Text = "첨부물을 선택하세요";
            m_claimButton.Enabled = false;
            return;
        }

        if (attachment.AttachmentType == 1)
        {
            m_attachmentDetailLabel.Text =
                $"{ItemCatalog.GetName(attachment.ItemDataId)} × {attachment.Quantity:N0}\n" +
                $"분류  {ItemCatalog.GetCategoryName(attachment.ItemDataId)}\n{FormatItemStatsDetail(attachment.ItemData)}";
        }
        else
        {
            m_attachmentDetailLabel.Text =
                $"{GetCurrencyName(attachment.CurrencyId)} × {attachment.CurrencyAmount:N0}";
        }
        m_claimButton.Enabled = attachment.State == 1;
    }

    private void ClearMailDetail()
    {
        m_selectedMail = null;
        m_mailSubjectLabel.Text = "우편을 선택하세요";
        m_mailBodyTextBox.Clear();
        m_attachmentGrid.Rows.Clear();
        m_attachmentDetailLabel.Text = "첨부물을 선택하세요";
        m_claimButton.Enabled = false;
    }

    private void ShowSelectedInventoryDetail()
    {
        if (m_inventoryGrid.CurrentRow?.Tag is not InventoryItem item)
        {
            ClearInventoryDetail();
            return;
        }

        m_inventoryDetailNameLabel.Text = $"{ItemCatalog.GetName(item.ItemDataId)} × {item.Quantity:N0}";
        m_inventoryDetailStatsLabel.Text =
            $"분류  {ItemCatalog.GetCategoryName(item.ItemDataId)}\n{FormatItemStatsDetail(item.ItemData)}";
        m_inventoryDetailStateLabel.Text =
            $"거래 가능: {(item.IsTradable ? "예" : "아니요")}\n장착 상태: {(item.IsEquipped ? "장착 중" : "미장착")}";
    }

    private void ClearInventoryDetail()
    {
        m_inventoryDetailNameLabel.Text = "아이템을 선택하세요";
        m_inventoryDetailStatsLabel.Text = string.Empty;
        m_inventoryDetailStateLabel.Text = string.Empty;
    }

    private async Task RegisterListingAsync()
    {
		if ((uint)m_activeListingCount >= m_maxActiveListings)
		{
			MessageBox.Show(this,
				$"동시에 등록할 수 있는 매물은 최대 {m_maxActiveListings}개입니다.",
				"판매 등록 한도",
				MessageBoxButtons.OK,
				MessageBoxIcon.Information);
			return;
		}
        if (m_inventoryGrid.CurrentRow?.Tag is not InventoryItem item) return;
		ulong startPrice = (ulong)m_startPriceInput.Value;
		ulong buyoutPrice = (ulong)m_buyoutPriceInput.Value;
		if (startPrice < m_minimumListingPrice || startPrice > m_maximumListingPrice)
		{
			MessageBox.Show(this,
				$"시작가는 {m_minimumListingPrice:N0} 이상 {m_maximumListingPrice:N0} 이하여야 합니다.",
				"판매 등록",
				MessageBoxButtons.OK,
				MessageBoxIcon.Information);
			return;
		}
		if (buyoutPrice != 0 && (buyoutPrice < startPrice || buyoutPrice > m_maximumListingPrice))
		{
			MessageBox.Show(this,
				$"즉시구매가는 0(사용 안 함)이거나 시작가 이상 {m_maximumListingPrice:N0} 이하여야 합니다.",
				"판매 등록",
				MessageBoxButtons.OK,
				MessageBoxIcon.Information);
			return;
		}
        await RunUiOperationAsync(async () =>
        {
            ListingRegisterResult result = await m_client.RegisterListingAsync(
				item, m_defaultCurrencyId, startPrice, buyoutPrice, (uint)m_durationInput.Value);
            EnsureSuccess(result.ResultCode, "경매 등록");
			UpdateListingLimit(m_activeListingCount + 1);
            ShowToast($"경매 등록 완료  ·  매물 {result.ListingId:N0}");
            m_myListingsButton.HasNotification = true;
            await RefreshInventoryAsync();
        }, "경매 등록 완료");
    }

	private void UpdateListingLimit(int activeListingCount)
	{
		int maximum = checked((int)m_maxActiveListings);
		m_activeListingCount = Math.Clamp(activeListingCount, 0, maximum);
		bool limitReached = m_activeListingCount >= maximum;
		m_listingLimitLabel.Text = $"판매 등록 {m_activeListingCount} / {maximum}";
		m_listingLimitLabel.ForeColor = limitReached ? Color.IndianRed : s_muted;
		m_registerListingButton.Enabled = !limitReached;
	}

	private void ApplyAuctionPolicy(AuctionAuthResult auth)
	{
		if (auth.MaxActiveListings == 0 || auth.MaxActiveListings >= 100 ||
			auth.SearchPageSize == 0 || auth.SearchPageSize >= 100 ||
			auth.InventoryListPageSize == 0 ||
			auth.MailListPageSize == 0 ||
			auth.MinimumListingDurationSeconds == 0 ||
			auth.MinimumListingDurationSeconds > auth.MaximumListingDurationSeconds ||
			auth.DefaultListingDurationSeconds < auth.MinimumListingDurationSeconds ||
			auth.DefaultListingDurationSeconds > auth.MaximumListingDurationSeconds ||
			auth.DefaultCurrencyId == 0 ||
			auth.MinimumBidIncrement == 0 ||
			auth.MinimumListingPrice == 0 ||
			auth.MinimumListingPrice > auth.MaximumListingPrice)
		{
			throw new InvalidDataException("AuctionServer가 잘못된 경매장 기획 데이터를 전달했습니다.");
		}

		m_maxActiveListings = auth.MaxActiveListings;
		m_searchPageSize = checked((int)auth.SearchPageSize);
		m_defaultCurrencyId = auth.DefaultCurrencyId;
		m_minimumBidIncrement = auth.MinimumBidIncrement;
		m_minimumListingPrice = auth.MinimumListingPrice;
		m_maximumListingPrice = auth.MaximumListingPrice;
		m_durationInput.Minimum = auth.MinimumListingDurationSeconds;
		m_durationInput.Maximum = auth.MaximumListingDurationSeconds;
		m_durationInput.Value = auth.DefaultListingDurationSeconds;

		decimal minimumPrice = auth.MinimumListingPrice;
		decimal maximumPrice = auth.MaximumListingPrice;
		decimal bidIncrement = auth.MinimumBidIncrement;
		m_startPriceInput.Maximum = maximumPrice;
		m_startPriceInput.Minimum = minimumPrice;
		m_startPriceInput.Value = minimumPrice;
		m_buyoutPriceInput.Minimum = 0;
		m_buyoutPriceInput.Maximum = maximumPrice;
		m_buyoutPriceInput.Value = 0;
		m_bidAmountInput.Minimum = minimumPrice;
		m_bidAmountInput.Maximum = ulong.MaxValue;
		m_bidAmountInput.Increment = bidIncrement;
		m_bidAmountInput.Value = minimumPrice;
		m_balanceLabel.Text = $"{GetCurrencyName(m_defaultCurrencyId)} -";
		UpdateListingLimit(0);
	}

	private void ConfigureBidInput(ListingDetail listing)
	{
		ulong maximumBid = ulong.MaxValue;
		if (listing.BuyoutPrice > 0)
			maximumBid = Math.Min(maximumBid, listing.BuyoutPrice - 1);

		if (listing.CurrentBidPrice != 0 && listing.CurrentBidPrice > ulong.MaxValue - m_minimumBidIncrement)
		{
			m_bidAmountInput.Minimum = 0;
			m_bidAmountInput.Maximum = ulong.MaxValue;
			m_bidAmountInput.Value = ulong.MaxValue;
			m_bidButton.Enabled = false;
			return;
		}

		ulong minimumBid = listing.CurrentBidPrice == 0
			? Math.Max(listing.StartPrice, m_minimumListingPrice)
			: listing.CurrentBidPrice + m_minimumBidIncrement;

		m_bidAmountInput.Minimum = 0;
		m_bidAmountInput.Maximum = maximumBid;
		m_bidAmountInput.Increment = m_minimumBidIncrement;
		if (minimumBid > maximumBid)
		{
			m_bidAmountInput.Value = maximumBid;
			m_bidButton.Enabled = false;
			return;
		}

		m_bidAmountInput.Minimum = minimumBid;
		m_bidAmountInput.Value = minimumBid;
		m_bidButton.Enabled = true;
	}

    private async Task RunUiOperationAsync(Func<Task> operation, string successMessage, bool showErrorOnly = false)
    {
        try
        {
            UseWaitCursor = true;
            await operation();
            if (!showErrorOnly) SetStatus(successMessage);
        }
        catch (Exception exception)
        {
            SetStatus(exception.Message);
            if (m_loginPanel.Visible)
            {
                m_loginStatusLabel.Text = $"● 연결 실패 · {exception.Message}";
            }
            MessageBox.Show(this, exception.Message, "Auction Client", MessageBoxButtons.OK, MessageBoxIcon.Warning);
        }
        finally
        {
            UseWaitCursor = false;
        }
    }

    private void ShowView(Control view, NavigationButton selectedButton)
    {
        foreach (Control child in m_contentHost.Controls) child.Visible = ReferenceEquals(child, view);
        foreach (NavigationButton button in new[] { m_auctionButton, m_saleHistoryButton, m_myListingsButton, m_myBidsButton, m_mailButton, m_sellButton })
        {
            button.BackColor = ReferenceEquals(button, selectedButton) ? Color.FromArgb(63, 50, 30) : s_panel;
            button.ForeColor = ReferenceEquals(button, selectedButton) ? s_gold : s_text;
        }
        view.BringToFront();
    }

    private static void SelectFirstRow(DataGridView grid) =>
        SelectRow(grid, grid.Rows.Cast<DataGridViewRow>().FirstOrDefault());

    private static void SelectRow(DataGridView grid, DataGridViewRow? row)
    {
        grid.ClearSelection();
        if (row is null || row.Cells.Count == 0)
        {
            return;
        }

        row.Selected = true;
        grid.CurrentCell = row.Cells[0];
    }

    private void ShowToast(string message)
    {
        m_toastLabel.Text = message;
        m_toastLabel.Visible = true;
        m_toastLabel.BringToFront();
        m_toastTimer.Stop();
        m_toastTimer.Start();
    }

    private void SetStatus(string message) => m_statusStripLabel.Text = $"{DateTime.Now:HH:mm:ss}  {message}";

    private void Ui(Action action)
    {
        if (IsDisposed) return;
        if (InvokeRequired) BeginInvoke(action); else action();
    }

    private static void EnsureSuccess(ushort resultCode, string operation)
    {
        if (resultCode != 0) throw new InvalidOperationException($"{operation} 실패: {GetResultText(resultCode)} ({resultCode})");
    }

    private static string GetResultText(ushort code) => code switch
    {
        0 => "SUCCESS",
        1 => "SERVER_BUSY",
		2 => "INVALID_REQUEST",
        3 => "DATABASE_UNAVAILABLE",
		4 => "BID_NOT_CLAIMABLE",
		5 => "CURRENCY_LIMIT_EXCEEDED",
		6 => "PARTIAL_COMMIT",
		7 => "INTERNAL_ERROR",
        8 => "AUTH_REQUIRED",
        9 => "AUTHENTICATION_FAILED",
        10 => "ALREADY_AUTHENTICATED",
		11 => "INVENTORY_ITEM_NOT_FOUND",
		12 => "ITEM_NOT_TRADABLE",
        13 => "ITEM_VERSION_MISMATCH",
		14 => "ITEM_EQUIPPED",
        15 => "LISTING_NOT_FOUND",
		16 => "BID_TOO_LOW",
		17 => "INSUFFICIENT_CURRENCY",
		18 => "SELLER_CANNOT_BID",
        19 => "LISTING_VERSION_MISMATCH",
		20 => "BUYOUT_NOT_AVAILABLE",
		21 => "SELLER_CANNOT_BUY",
		22 => "MAIL_NOT_FOUND",
		23 => "MAIL_ATTACHMENT_NOT_CLAIMABLE",
		24 => "INVENTORY_FULL",
		25 => "ITEM_INSTANCE_CONFLICT",
		26 => "NOT_LISTING_OWNER",
		27 => "CANCEL_NOT_AVAILABLE",
		28 => "HIGHEST_BID_EXISTS",
		29 => "EXPIRE_NOT_AVAILABLE",
		30 => "LISTING_LIMIT_EXCEEDED",
            31 => "BID_STATE_INVALID",
            32 => "REQUEST_IN_PROGRESS",
            33 => "BID_REQUIRES_BUYOUT",
            _ => "UNKNOWN_ERROR"
    };

    private static string GetBidStateText(byte state) => state switch
    {
		1 => "입찰 처리 중",
		2 => "최고 입찰",
		3 => "상위 입찰됨 · 회수 가능",
		4 => "회수 처리 중",
		5 => "회수 완료",
		6 => "낙찰",
		7 => "입찰 실패",
        _ => $"상태 {state}"
    };

    private static string GetMailStateText(byte state) => state switch
    {
		1 => "미수령",
		2 => "일부 수령",
		3 => "수령 완료",
		4 => "만료",
        _ => $"상태 {state}"
    };

	private static string GetMailAttachmentStateText(byte state) => state switch
	{
		1 => "수령 가능",
		2 => "수령 완료",
		_ => $"상태 {state}"
	};

	private static ulong GetDisplayedCurrentPrice(ulong startPrice, ulong currentBidPrice) =>
		currentBidPrice == 0 ? startPrice : currentBidPrice;

    private static string GetCurrencyName(ushort currencyId) => CurrencyCatalog.GetName(currencyId);

    private static string FormatItemStatsInline(string itemData)
    {
        (uint strength, uint dexterity, uint intelligence, uint luck) = ReadItemStats(itemData);
        List<string> stats = [];
        if (strength > 0) stats.Add($"STR {strength}");
        if (dexterity > 0) stats.Add($"DEX {dexterity}");
        if (intelligence > 0) stats.Add($"INT {intelligence}");
        if (luck > 0) stats.Add($"LUK {luck}");
        return stats.Count == 0 ? "-" : string.Join(" · ", stats);
    }

    private static string FormatItemStatsDetail(string itemData)
    {
        (uint strength, uint dexterity, uint intelligence, uint luck) = ReadItemStats(itemData);
        return $"STR  {strength:N0}\nDEX  {dexterity:N0}\nINT  {intelligence:N0}\nLUK  {luck:N0}";
    }

    private static (uint Strength, uint Dexterity, uint Intelligence, uint Luck) ReadItemStats(string itemData)
    {
        if (string.IsNullOrWhiteSpace(itemData))
        {
            return default;
        }

        try
        {
            using JsonDocument document = JsonDocument.Parse(itemData);
            JsonElement root = document.RootElement;
            return (
                ReadUInt32(root, "str"),
                ReadUInt32(root, "dex"),
                ReadUInt32(root, "int"),
                ReadUInt32(root, "luk"));
        }
        catch (JsonException)
        {
            return default;
        }
    }

    private static uint ReadUInt32(JsonElement root, string propertyName) =>
        root.TryGetProperty(propertyName, out JsonElement property) && property.TryGetUInt32(out uint value)
            ? value
            : 0;

    private static string FormatStats(ListingSummary listing)
    {
        List<string> stats = [];
        if (listing.Strength > 0) stats.Add($"STR {listing.Strength}");
        if (listing.Dexterity > 0) stats.Add($"DEX {listing.Dexterity}");
        if (listing.Intelligence > 0) stats.Add($"INT {listing.Intelligence}");
        if (listing.Luck > 0) stats.Add($"LUK {listing.Luck}");
        return stats.Count == 0 ? "-" : string.Join(" · ", stats);
    }

    private static string FormatStats(SaleHistorySummary sale)
    {
        List<string> stats = [];
        if (sale.Strength > 0) stats.Add($"STR {sale.Strength}");
        if (sale.Dexterity > 0) stats.Add($"DEX {sale.Dexterity}");
        if (sale.Intelligence > 0) stats.Add($"INT {sale.Intelligence}");
        if (sale.Luck > 0) stats.Add($"LUK {sale.Luck}");
        return stats.Count == 0 ? "-" : string.Join(" · ", stats);
    }

    private static string GetSaleTypeText(byte saleType) => saleType switch
    {
        1 => "낙찰",
        2 => "즉시 구매",
        _ => $"거래 유형 {saleType}"
    };

    private static string FormatRemaining(ulong unixMilliseconds)
    {
        TimeSpan remaining = DateTimeOffset.FromUnixTimeMilliseconds((long)unixMilliseconds) - DateTimeOffset.UtcNow;
        if (remaining <= TimeSpan.Zero) return "만료";
        return remaining.TotalDays >= 1 ? $"{(int)remaining.TotalDays}일 {remaining.Hours}시간" : $"{remaining.Hours}시간 {remaining.Minutes}분";
    }

    private static string FormatDate(ulong unixMilliseconds) =>
        DateTimeOffset.FromUnixTimeMilliseconds((long)unixMilliseconds).ToLocalTime().ToString("yyyy-MM-dd HH:mm");

    private static NumericUpDown CreateNumericInput(decimal maximum) => new()
    {
        Minimum = 0,
        Maximum = maximum,
        ThousandsSeparator = true,
        Height = 32
    };

    private static DataGridView CreateGrid() => new()
    {
        Dock = DockStyle.Fill,
        ReadOnly = true,
        AllowUserToAddRows = false,
        AllowUserToDeleteRows = false,
        AllowUserToResizeRows = false,
        AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode.Fill,
        SelectionMode = DataGridViewSelectionMode.FullRowSelect,
        MultiSelect = false,
        RowHeadersVisible = false,
        BackgroundColor = s_panel,
        BorderStyle = BorderStyle.FixedSingle
    };

    private static TableLayoutPanel CreateCardLayout() => new()
    {
        Dock = DockStyle.Fill,
        Padding = new Padding(22),
        AutoScroll = true,
        BackColor = s_panelLight,
        ColumnCount = 1
    };

    private static FlowLayoutPanel CreateSectionHeader(string title, Button action)
    {
        FlowLayoutPanel panel = new() { Dock = DockStyle.Fill, FlowDirection = FlowDirection.LeftToRight };
        panel.Controls.Add(new Label { Text = title, AutoSize = true, Font = new Font("맑은 고딕", 16, FontStyle.Bold), ForeColor = s_gold, Margin = new Padding(0, 8, 24, 0) });
        action.Width = 110;
        action.Height = 34;
        panel.Controls.Add(action);
        return panel;
    }

    private static void AddFilter(FlowLayoutPanel parent, string label, Control input, int width)
    {
        TableLayoutPanel field = new() { Width = width, Height = 65, RowCount = 2, Margin = new Padding(0, 0, 10, 0) };
        field.RowStyles.Add(new RowStyle(SizeType.Absolute, 25));
        field.RowStyles.Add(new RowStyle(SizeType.Absolute, 35));
        field.Controls.Add(CreateLabel(label));
        input.Dock = DockStyle.Fill;
        field.Controls.Add(input);
        parent.Controls.Add(field);
    }

    private static Label CreateLabel(string text) => new() { Text = text, AutoSize = true, ForeColor = s_muted };

    private static Control CreateSpacer() => new Panel { Width = 28, Height = 1 };

    private static void ApplyTheme(Control root)
    {
        foreach (Control control in root.Controls)
        {
            switch (control)
            {
            case Button button:
                button.FlatStyle = FlatStyle.Flat;
                button.FlatAppearance.BorderColor = s_border;
                button.BackColor = s_panelLight;
                button.ForeColor = s_text;
                break;
            case TextBox textBox:
                textBox.BackColor = Color.FromArgb(12, 17, 22);
                textBox.ForeColor = s_text;
                textBox.BorderStyle = BorderStyle.FixedSingle;
                break;
            case NumericUpDown numeric:
                numeric.BackColor = Color.FromArgb(12, 17, 22);
                numeric.ForeColor = s_text;
                break;
            case ComboBox combo:
                combo.BackColor = Color.FromArgb(12, 17, 22);
                combo.ForeColor = s_text;
                break;
            case DataGridView grid:
                grid.DefaultCellStyle.BackColor = s_panel;
                grid.DefaultCellStyle.ForeColor = s_text;
                grid.DefaultCellStyle.SelectionBackColor = Color.FromArgb(79, 61, 34);
                grid.DefaultCellStyle.SelectionForeColor = Color.White;
                grid.ColumnHeadersDefaultCellStyle.BackColor = s_panelLight;
                grid.ColumnHeadersDefaultCellStyle.ForeColor = s_text;
                grid.EnableHeadersVisualStyles = false;
                grid.GridColor = s_border;
                grid.RowTemplate.Height = 34;
                break;
            }
            ApplyTheme(control);
        }
    }
}
