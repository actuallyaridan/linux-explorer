#include "PerformancePage.h"
#include "IconHelper.h"
#include "Win7Ui.h"
#include "Branding.h"

#include <QScrollArea>
#include <QLabel>
#include <QPushButton>
#include <QDialog>
#include <QProgressBar>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QFrame>
#include <QFont>
#include <QLocale>
#include <QEvent>

namespace {

// The five WEI components, in canonical Windows order.
struct Component { const char *name; const char *rated; };
const Component kComponents[5] = {
    { "Processor:",         "Calculations per second" },
    { "Memory (RAM):",      "Memory operations per second" },
    { "Graphics:",          "Desktop performance for Linux Aero" },
    { "Gaming graphics:",   "3D business and gaming graphics performance" },
    { "Primary hard disk:", "Disk data transfer rate" },
};

QString fmt1(double v) { return QLocale::c().toString(v, 'f', 1); }

} // namespace

QList<SidebarLink> PerformancePage::sidebarLinks()
{
    return {
        Nav::plain("Adjust visual effects"),
        Nav::plain("Adjust indexing options"),
        Nav::plain("Adjust power settings"),
        Nav::plain("Open disk cleanup"),
        Nav::plain("Advanced tools"),
    };
}

QList<SidebarLink> PerformancePage::sidebarSeeAlso()
{
    return { Nav::to("Action Center", PageId::ActionCenter) };
}

PerformancePage::PerformancePage(QScrollArea *sidebar, QWidget *parent)
    : QWidget(parent)
{
    m_bench = new WeiBenchmark(this);

    auto *v = Win7::pageScaffold(this, sidebar, /*bottomMargin=*/20,
                                 /*fixedWidth=*/-1, /*topMargin=*/16);

    auto linkStyle = QStringLiteral(
        "QLabel { color: #1F4E99; background: transparent; }"
        "QLabel:hover { color: #0033AA; }");

    // Title + subtitle
    v->addWidget(Win7::pageTitle("Rate and improve your computer's performance",
                                 13, "#1A5DAB"));
    v->addSpacing(12);

    v->addWidget(Win7::label(Branding::brand(
        "The Linux Experience Index assesses key system components on a scale "
        "of 1.0 to 9.9.")));
    v->addSpacing(14);

    // Yellow "not yet established" banner (unrated only)
    m_banner = new QFrame;
    m_banner->setObjectName("weiBanner");
    m_banner->setStyleSheet(
        "#weiBanner { background: #FCFCD7; border: 1px solid #DEDCA8; }");
    auto *bn = new QHBoxLayout(m_banner);
    bn->setContentsMargins(10, 5, 6, 5);
    bn->setSpacing(8);
    auto *bannerText = new QLabel(Branding::brand(
        "Your Linux Experience Index has not yet been established."));
    { QFont f = bannerText->font(); f.setPointSize(9); bannerText->setFont(f); }
    bannerText->setStyleSheet("color: #000000; background: transparent;");
    bn->addWidget(bannerText, 0, Qt::AlignVCenter);
    bn->addStretch(1);
    m_rateBtn = new QPushButton("Rate this computer");
    m_rateBtn->setIcon(themeIcon({ "security-high", "dialog-password",
                                   "preferences-system" }));
    m_rateBtn->setCursor(Qt::PointingHandCursor);
    connect(m_rateBtn, &QPushButton::clicked, this, &PerformancePage::onRateClicked);
    bn->addWidget(m_rateBtn, 0, Qt::AlignVCenter);
    v->addWidget(m_banner);
    v->addSpacing(14);

    // Score table: one grid carries everything so rows line up. Columns:
    //   0 Component | 1 What is rated | 2 Subscore | 3 Base-score panel
    // A shaded frame sits *behind* the last two columns (added before the data
    // cells so it paints underneath the transparent labels on top).
    auto *table = new QGridLayout;
    table->setContentsMargins(0, 0, 0, 0);
    table->setHorizontalSpacing(20);
    table->setVerticalSpacing(0);
    table->setColumnStretch(1, 1);

    auto headerCell = [](const QString &t, int align) {
        auto *l = new QLabel(t);
        QFont f = l->font(); f.setPointSize(9); f.setBold(true); l->setFont(f);
        l->setStyleSheet("color: #000000; background: transparent;");
        l->setAlignment(static_cast<Qt::Alignment>(align) | Qt::AlignVCenter);
        return l;
    };
    table->addWidget(headerCell("Component",     Qt::AlignLeft),  0, 0);
    table->addWidget(headerCell("What is rated", Qt::AlignLeft),  0, 1);
    m_subHeader  = headerCell("Subscore",   Qt::AlignRight);
    m_baseHeader = headerCell("Base score", Qt::AlignHCenter);
    table->addWidget(m_subHeader,  0, 2);
    table->addWidget(m_baseHeader, 0, 3);

    auto *hr = new QFrame;
    hr->setFrameShape(QFrame::HLine);
    hr->setFixedHeight(1);
    hr->setStyleSheet("QFrame { background: #C9C9C9; border: none; }");
    table->addWidget(hr, 1, 0, 1, 4);

    // Shaded backdrop behind the Subscore + Base-score columns (rows 2–6).
    m_shade = new QFrame;
    m_shade->setObjectName("weiShade");
    m_shade->setStyleSheet(
        "#weiShade { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        " stop:0 #EDF3FB, stop:1 #DCE6F5); border: 1px solid #C7D6EC; }");
    table->addWidget(m_shade, 2, 2, 5, 2);

    for (int i = 0; i < 5; ++i) {
        const int row = 2 + i;
        auto *name = new QLabel(kComponents[i].name);
        QFont nf = name->font(); nf.setPointSize(9); nf.setBold(true); name->setFont(nf);
        name->setStyleSheet("color: #1A1A1A; background: transparent;");
        table->addWidget(name, row, 0, Qt::AlignVCenter);

        auto *rated = new QLabel(Branding::brand(kComponents[i].rated));
        QFont rf = rated->font(); rf.setPointSize(9); rated->setFont(rf);
        rated->setWordWrap(true);
        rated->setStyleSheet("color: #000000; background: transparent;");
        table->addWidget(rated, row, 1, Qt::AlignVCenter);

        auto *sub = new QLabel("(unrated)");
        QFont sf = sub->font(); sf.setPointSize(9); sub->setFont(sf);
        sub->setStyleSheet("color: #555555; background: transparent;");
        sub->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        sub->setContentsMargins(0, 0, 8, 0);
        table->addWidget(sub, row, 2, Qt::AlignRight | Qt::AlignVCenter);
        m_subscore[i] = sub;

        auto *sep = new QFrame;
        sep->setFrameShape(QFrame::HLine);
        sep->setFixedHeight(1);
        sep->setStyleSheet("QFrame { background: #D6E0EF; border: none; }");
        table->addWidget(sep, row, 0, 1, 2, Qt::AlignBottom);
    }
    for (int row = 2; row < 7; ++row)
        table->setRowMinimumHeight(row, 30);

    // Base-score panel (the glossy badge + caption) spanning the data rows.
    m_basePanel = new QFrame;
    m_basePanel->setStyleSheet("background: transparent; border: none;");
    auto *bp = new QVBoxLayout(m_basePanel);
    bp->setContentsMargins(10, 6, 10, 6);
    bp->setSpacing(4);
    bp->addStretch(1);

    m_badge = new QFrame;
    m_badge->setObjectName("weiBadge");
    m_badge->setFixedSize(78, 72);
    m_badge->setStyleSheet(
        "#weiBadge { border: 1px solid #2C5C98; border-radius: 6px;"
        " background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        " stop:0 #7FB0E4, stop:0.49 #3F7CC2, stop:0.50 #2E68B0, stop:1 #5B93D0); }");
    auto *badgeLay = new QVBoxLayout(m_badge);
    badgeLay->setContentsMargins(0, 0, 0, 0);
    m_baseBig = new QLabel("-");
    { QFont f = m_baseBig->font(); f.setPointSize(30); f.setBold(true); m_baseBig->setFont(f); }
    m_baseBig->setStyleSheet("color: #FFFFFF; background: transparent; border: none;");
    m_baseBig->setAlignment(Qt::AlignCenter);
    badgeLay->addWidget(m_baseBig);
    bp->addWidget(m_badge, 0, Qt::AlignHCenter);

    m_baseCaption = new QLabel("Determined by\nlowest subscore");
    { QFont f = m_baseCaption->font(); f.setPointSize(8); m_baseCaption->setFont(f); }
    m_baseCaption->setStyleSheet("color: #1A1A1A; background: transparent; border: none;");
    m_baseCaption->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    bp->addWidget(m_baseCaption, 0, Qt::AlignHCenter);
    bp->addStretch(1);

    table->addWidget(m_basePanel, 2, 3, 5, 1);

    auto *tableWrap = new QWidget;
    tableWrap->setStyleSheet("background: transparent;");
    tableWrap->setLayout(table);
    v->addWidget(tableWrap);
    v->addSpacing(16);

    // Help items (left) + "View and print..." (right)
    auto helpItem = [&](const QString &text) {
        auto *rowW = new QWidget;
        rowW->setStyleSheet("background: transparent;");
        auto *h = new QHBoxLayout(rowW);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(8);
        auto *ic = new QLabel;
        ic->setFixedSize(20, 20);
        ic->setPixmap(themeIcon({ "help-about", "help-contents", "help-browser",
                                  "dialog-question" }).pixmap(20, 20));
        ic->setStyleSheet("background: transparent;");
        h->addWidget(ic, 0, Qt::AlignVCenter);
        auto *l = new QLabel(text);
        QFont f = l->font(); f.setPointSize(9); l->setFont(f);
        l->setStyleSheet(linkStyle);
        l->setCursor(Qt::PointingHandCursor);
        h->addWidget(l, 0, Qt::AlignVCenter);
        h->addStretch(1);
        return rowW;
    };

    auto *helpRow = new QHBoxLayout;
    helpRow->setContentsMargins(0, 0, 0, 0);
    helpRow->setSpacing(16);

    auto *helpCol = new QVBoxLayout;
    helpCol->setContentsMargins(0, 0, 0, 0);
    helpCol->setSpacing(8);
    helpCol->addWidget(helpItem("What do these numbers mean?"));
    helpCol->addWidget(helpItem("Tips for improving your computer's performance."));
    helpRow->addLayout(helpCol, 1);

    // "View and print detailed…" with a printer icon (toggles raw metrics).
    auto *vpWrap = new QWidget;
    vpWrap->setStyleSheet("background: transparent;");
    auto *vph = new QHBoxLayout(vpWrap);
    vph->setContentsMargins(0, 0, 0, 0);
    vph->setSpacing(8);
    auto *printerIc = new QLabel;
    printerIc->setFixedSize(28, 28);
    printerIc->setPixmap(themeIcon({ "printer", "document-print",
                                     "print-preview" }).pixmap(28, 28));
    printerIc->setStyleSheet("background: transparent;");
    vph->addWidget(printerIc, 0, Qt::AlignTop);
    m_detailToggle = new QLabel(
        "View and print detailed performance and\nsystem information");
    { QFont f = m_detailToggle->font(); f.setPointSize(9); m_detailToggle->setFont(f); }
    m_detailToggle->setStyleSheet(linkStyle);
    m_detailToggle->setCursor(Qt::PointingHandCursor);
    m_detailToggle->installEventFilter(this);
    vph->addWidget(m_detailToggle, 0, Qt::AlignTop);
    helpRow->addWidget(vpWrap, 0, Qt::AlignTop);

    v->addLayout(helpRow);
    v->addSpacing(8);

    // Collapsible raw-metrics detail (our addition, under the help row).
    m_detailWrap = new QWidget;
    m_detailWrap->setStyleSheet("background: transparent;");
    auto *dv = new QVBoxLayout(m_detailWrap);
    dv->setContentsMargins(0, 4, 0, 4);
    dv->setSpacing(0);
    m_detailLabel = new QLabel;
    { QFont f = m_detailLabel->font(); f.setPointSize(9); f.setFamily("monospace");
      m_detailLabel->setFont(f); }
    m_detailLabel->setStyleSheet("color: #222222; background: transparent;");
    m_detailLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    dv->addWidget(m_detailLabel);
    m_detailWrap->hide();
    v->addWidget(m_detailWrap);
    v->addSpacing(10);

    // "Learn more about scores and software online" box
    auto *learnBox = new QFrame;
    learnBox->setObjectName("learnBox");
    learnBox->setStyleSheet(
        "#learnBox { background: #FFFFFF; border: 1px solid #D9D9D9; }");
    auto *lh = new QHBoxLayout(learnBox);
    lh->setContentsMargins(12, 12, 12, 12);
    lh->setSpacing(12);
    auto *learnIc = new QLabel;
    learnIc->setFixedSize(32, 32);
    learnIc->setPixmap(themeIcon({ "applications-internet", "internet-web-browser",
                                   "emblem-web", "text-html" }).pixmap(32, 32));
    learnIc->setStyleSheet("background: transparent;");
    lh->addWidget(learnIc, 0, Qt::AlignVCenter);
    auto *learnLink = new QLabel("Learn more about scores and software\nonline");
    { QFont f = learnLink->font(); f.setPointSize(9); learnLink->setFont(f); }
    learnLink->setStyleSheet(linkStyle);
    learnLink->setCursor(Qt::PointingHandCursor);
    lh->addWidget(learnLink, 0, Qt::AlignVCenter);
    lh->addStretch(1);
    v->addWidget(learnBox);
    v->addSpacing(12);

    // Bottom row: "scores are current" (left) + Re-run link (right)
    auto *bottomRow = new QHBoxLayout;
    bottomRow->setContentsMargins(0, 0, 0, 0);
    bottomRow->setSpacing(8);
    m_scoresCurrent = new QLabel;
    { QFont f = m_scoresCurrent->font(); f.setPointSize(9); m_scoresCurrent->setFont(f); }
    m_scoresCurrent->setStyleSheet("color: #000000; background: transparent;");
    bottomRow->addWidget(m_scoresCurrent, 0, Qt::AlignVCenter);
    bottomRow->addStretch(1);

    auto *rerunIc = new QLabel;
    rerunIc->setFixedSize(16, 16);
    rerunIc->setPixmap(themeIcon({ "security-high", "view-refresh",
                                   "system-run" }).pixmap(16, 16));
    rerunIc->setStyleSheet("background: transparent;");
    bottomRow->addWidget(rerunIc, 0, Qt::AlignVCenter);
    m_rerunLink = new QLabel("Re-run the assessment");
    { QFont f = m_rerunLink->font(); f.setPointSize(9); m_rerunLink->setFont(f); }
    m_rerunLink->setStyleSheet(linkStyle);
    m_rerunLink->setCursor(Qt::PointingHandCursor);
    m_rerunLink->installEventFilter(this);
    bottomRow->addWidget(m_rerunLink, 0, Qt::AlignVCenter);
    v->addLayout(bottomRow);

    v->addStretch(1);

    // Engine wiring
    connect(m_bench, &WeiBenchmark::progress, this,
        [this](int pct, const QString &phase) {
            if (!m_dlg) return;
            if (m_dlgBar)   m_dlgBar->setValue(pct);
            if (m_dlgTitle) m_dlgTitle->setText(phase);
        });
    connect(m_bench, &WeiBenchmark::finished, this,
        [this](const WeiResult &r) {
            if (m_dlg) { m_dlg->close(); m_dlg->deleteLater();
                         m_dlg = nullptr; m_dlgBar = nullptr; m_dlgTitle = nullptr; }
            if (m_canceled) return;
            m_result = r;
            populate(r);
            applyState(/*rated=*/true);
        });

    // Show any previously cached assessment.
    m_result = WeiResult::load();
    if (m_result.valid) {
        populate(m_result);
        applyState(/*rated=*/true);
    } else {
        applyState(/*rated=*/false);
    }
}

// Clicks: the detail link toggles raw metrics; the re-run link starts a run.
bool PerformancePage::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonRelease) {
        if (watched == m_detailToggle) {
            m_detailWrap->setVisible(!m_detailWrap->isVisible());
            return true;
        }
        if (watched == m_rerunLink) {
            onRateClicked();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

// Switch between the unrated (yellow banner) and rated (badge + scores) views.
void PerformancePage::applyState(bool rated)
{
    m_banner->setVisible(!rated);
    m_subHeader->setVisible(rated);
    m_baseHeader->setVisible(rated);
    m_shade->setVisible(rated);
    m_basePanel->setVisible(rated);
    m_detailToggle->setVisible(rated);
    m_scoresCurrent->setVisible(rated);
    m_rerunLink->setVisible(rated);
    if (!rated) {
        m_detailWrap->hide();
        for (auto *s : m_subscore) {
            s->setText("(unrated)");
            QFont f = s->font(); f.setPointSize(9); f.setBold(false); s->setFont(f);
            s->setStyleSheet("color: #555555; background: transparent;");
        }
        m_baseBig->setText("-");
    }
}

void PerformancePage::populate(const WeiResult &r)
{
    auto setSub = [&](int i, double score) {
        if (score <= 0.0) {
            m_subscore[i]->setText("(unrated)");
            QFont f = m_subscore[i]->font(); f.setPointSize(9); f.setBold(false);
            m_subscore[i]->setFont(f);
            m_subscore[i]->setStyleSheet("color: #555555; background: transparent;");
            return;
        }
        m_subscore[i]->setText(fmt1(score));
        QFont f = m_subscore[i]->font(); f.setPointSize(11); f.setBold(true);
        m_subscore[i]->setFont(f);
        m_subscore[i]->setStyleSheet("color: #000000; background: transparent;");
    };
    setSub(0, r.cpuScore);
    setSub(1, r.memoryScore);
    setSub(2, r.graphicsScore);
    setSub(3, r.gamingScore);
    setSub(4, r.diskScore);

    m_baseBig->setText(r.baseScore > 0.0 ? fmt1(r.baseScore) : "-");

    const QString when = r.assessedAt.isValid()
        ? r.assessedAt.toString("yyyy-MM-dd HH:mm:ss") : "-";
    m_scoresCurrent->setText(
        QStringLiteral("Your scores are current\nLast update: %1").arg(when));

    m_detailLabel->setText(Branding::brand(QString(
        "Linux Experience Index - raw measured metrics\n"
        "Assessed: %1\n"
        "\n"
        "  Processor (compress + encrypt) : %2 MB/s   -> %3\n"
        "  Memory (streaming copy)        : %4 MB/s   -> %5\n"
        "  Graphics (2D / Aero)           : %6 fps    -> %7\n"
        "  Gaming graphics (3D)           : %8 fps    -> %9\n"
        "  Primary hard disk (seq. read)  : %10 MB/s   -> %11\n"
        "\n"
        "  Base score (lowest subscore)   : %12")
        .arg(when)
        .arg(r.cpuThroughputMBs, 0, 'f', 0).arg(fmt1(r.cpuScore))
        .arg(r.memoryBandwidthMBs, 0, 'f', 0).arg(fmt1(r.memoryScore))
        .arg(r.graphicsFps, 0, 'f', 1).arg(fmt1(r.graphicsScore))
        .arg(r.gamingFps, 0, 'f', 1).arg(fmt1(r.gamingScore))
        .arg(r.diskReadMBs, 0, 'f', 0).arg(fmt1(r.diskScore))
        .arg(fmt1(r.baseScore))));
}

void PerformancePage::onRateClicked()
{
    if (m_bench->isRunning())
        return;
    m_canceled = false;

    // Custom modal dialog mirroring WinSAT's "Assessing…" window: a bold,
    // left-aligned phase title on its own shaded header band, the running-time
    // note below, a plain (textless) green bar, then a right-aligned Cancel.
    m_dlg = new QDialog(this);
    m_dlg->setWindowTitle(Branding::brand("Linux Experience Index"));
    m_dlg->setModal(true);
    m_dlg->setFixedWidth(400);

    auto *outer = new QVBoxLayout(m_dlg);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // Header band with the bold phase title.
    auto *header = new QFrame;
    header->setObjectName("dlgHeader");
    header->setStyleSheet(
        "#dlgHeader { background: #EDEDED; border-bottom: 1px solid #CFCFCF; }");
    auto *hl = new QVBoxLayout(header);
    hl->setContentsMargins(16, 11, 16, 11);
    hl->setSpacing(0);
    m_dlgTitle = new QLabel("Assessing system performance");
    { QFont f = m_dlgTitle->font(); f.setPointSize(10); f.setBold(true);
      m_dlgTitle->setFont(f); }
    m_dlgTitle->setStyleSheet("color: #1A1A1A; background: transparent;");
    m_dlgTitle->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    hl->addWidget(m_dlgTitle);
    outer->addWidget(header);

    // Body: note text, progress bar, Cancel.
    auto *body = new QWidget;
    auto *bl = new QVBoxLayout(body);
    bl->setContentsMargins(16, 12, 16, 12);
    bl->setSpacing(10);

    auto *note = new QLabel(
        "This might take a few minutes. Your screen might flash during the "
        "rating.");
    { QFont f = note->font(); f.setPointSize(9); note->setFont(f); }
    note->setWordWrap(true);
    note->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    note->setStyleSheet("color: #1A1A1A; background: transparent;");
    bl->addWidget(note);

    // Plain Qt progress bar, no custom styling.
    m_dlgBar = new QProgressBar;
    m_dlgBar->setRange(0, 100);
    m_dlgBar->setValue(0);
    bl->addWidget(m_dlgBar);

    outer->addWidget(body);

    // Footer panel (slightly darker Win7 command area) holding Cancel
    auto *footer = new QFrame;
    footer->setObjectName("dlgFooter");
    footer->setStyleSheet(
        "#dlgFooter { background: #F0F0F0; border-top: 1px solid #DFDFDF; }");
    auto *footH = new QHBoxLayout(footer);
    footH->setContentsMargins(12, 8, 12, 8);
    footH->setSpacing(8);
    footH->addStretch(1);
    auto *cancel = new QPushButton("Cancel");
    cancel->setMinimumWidth(75);
    cancel->setCursor(Qt::PointingHandCursor);
    footH->addWidget(cancel);
    outer->addWidget(footer);

    auto onCancel = [this]() {
        m_canceled = true;
        if (m_dlg) m_dlg->close();
    };
    connect(cancel, &QPushButton::clicked, this, onCancel);
    connect(m_dlg, &QDialog::rejected, this, onCancel);

    m_dlg->show();
    m_bench->run();
}
