#include "SoundDialog.h"
#include "IconHelper.h"
#include "Branding.h"

#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QMenu>
#include <QScrollArea>
#include <QFrame>
#include <QCheckBox>
#include <QRadioButton>
#include <QButtonGroup>
#include <QComboBox>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QDialogButtonBox>
#include <QFont>
#include <QProcess>
#include <QProcessEnvironment>
#include <QFile>
#include <QMessageBox>
#include <QSettings>
#include <QMouseEvent>
#include <QEvent>
#include <QRegularExpression>
#include <QPainter>
#include <QPixmap>
#include <QIcon>
#include <QPolygonF>

// Helpers
namespace {

// Run a read-only helper under a C locale so pactl's field labels stay parseable.
QString runTool(const QString &program, const QStringList &args)
{
    QProcess proc;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    proc.setProcessEnvironment(env);
    proc.start(program, args);
    if (!proc.waitForFinished(2500))
        return QString();
    return QString::fromUtf8(proc.readAllStandardOutput());
}

// Pick a device glyph from the name/description.
QIcon deviceIcon(const QString &label, bool sinks)
{
    const QString n = label.toLower();
    if (n.contains("head"))
        return themeIcon({"audio-headphones", "audio-headset", "audio-speakers"});
    if (!sinks || n.contains("mic"))
        return themeIcon({"audio-input-microphone", "audio-headset",
                          "audio-card"});
    return themeIcon({"audio-speakers", "audio-card",
                      "preferences-desktop-sound"});
}

// Compose the device glyph with the green "default" check badge, as Windows
// overlays on the default device.
QPixmap devicePixmap(const QIcon &icon, int size, bool isDefault)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.drawPixmap(0, 0, icon.pixmap(size, size));

    if (isDefault) {
        const double b = size * 0.46;
        const double x = size - b, y = size - b;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#FFFFFF"));
        p.drawEllipse(QRectF(x - 1, y - 1, b + 2, b + 2));
        p.setBrush(QColor("#3FA535"));
        p.drawEllipse(QRectF(x, y, b, b));

        QPen wp(QColor("#FFFFFF"), qMax(1.4, b * 0.14));
        wp.setCapStyle(Qt::RoundCap);
        wp.setJoinStyle(Qt::RoundJoin);
        p.setPen(wp);
        p.setBrush(Qt::NoBrush);
        QPolygonF check;
        check << QPointF(x + b * 0.24, y + b * 0.52)
              << QPointF(x + b * 0.42, y + b * 0.70)
              << QPointF(x + b * 0.78, y + b * 0.28);
        p.drawPolyline(check);
    }
    return pm;
}

} // namespace

// Data gathering
QList<SoundDialog::Device> SoundDialog::gatherDevices(bool sinks) const
{
    QList<Device> out;
    const QString text = runTool(QStringLiteral("pactl"),
        { QStringLiteral("list"), sinks ? QStringLiteral("sinks")
                                        : QStringLiteral("sources") });
    if (text.isEmpty())
        return out;

    static const QRegularExpression volRe(QStringLiteral("(\\d+)%"));
    Device cur;
    QString curName;
    bool in = false;

    auto flush = [&]() {
        // Skip monitor sources and the dummy sink; they aren't real devices.
        if (in && !curName.endsWith(QLatin1String(".monitor"))
            && curName != QLatin1String("auto_null"))
            out << cur;
    };

    const QList<QStringView> lines = QStringView(text).split(u'\n');
    for (const QStringView &raw : lines) {
        const QStringView line = raw.trimmed();
        if (line.startsWith(u"Sink #") || line.startsWith(u"Source #")) {
            flush();
            cur = Device();
            curName.clear();
            in = true;
        } else if (line.startsWith(u"Name:")) {
            curName = line.mid(5).trimmed().toString();
            cur.name = curName;
        } else if (line.startsWith(u"Description:")) {
            cur.description = line.mid(12).trimmed().toString();
        } else if (line.startsWith(u"Mute:")) {
            cur.muted = (line.mid(5).trimmed() == u"yes");
        } else if (line.startsWith(u"Volume:")) {
            const auto m = volRe.match(line.toString());
            if (m.hasMatch())
                cur.volumePercent = m.captured(1).toInt();
        } else if (line.startsWith(u"alsa.card_name = \"")) {
            QString v = line.mid(18).toString();
            if (v.endsWith(QLatin1Char('"')))
                v.chop(1);
            cur.cardName = v;
        }
    }
    flush();
    return out;
}

QString SoundDialog::defaultDevice(bool sinks) const
{
    return runTool(QStringLiteral("pactl"),
        { sinks ? QStringLiteral("get-default-sink")
                : QStringLiteral("get-default-source") }).trimmed();
}

// Dialog
SoundDialog::SoundDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Sound"));
    setWindowIcon(themeIcon({"preferences-desktop-sound", "audio-card",
                             "multimedia-volume-control"}));
    setModal(true);
    setStyleSheet("SoundDialog { background: #F0F0F0; }");

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(9, 9, 9, 9);
    root->setSpacing(8);

    m_tabs = new QTabWidget;
    m_tabs->addTab(buildDeviceTab(m_playback, true,
        QStringLiteral("Select a playback device below to modify its settings:")),
        QStringLiteral("Playback"));
    m_tabs->addTab(buildDeviceTab(m_recording, false,
        QStringLiteral("Select a recording device below to modify its settings:")),
        QStringLiteral("Recording"));
    m_tabs->addTab(buildSoundsTab(), QStringLiteral("Sounds"));
    m_tabs->addTab(buildCommunicationsTab(), QStringLiteral("Communications"));
    root->addWidget(m_tabs, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok
                                         | QDialogButtonBox::Cancel
                                         | QDialogButtonBox::Apply);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // Meter only the device on the visible tab (and don't hold the mic open
    // except while the Recording tab is shown).
    connect(m_tabs, &QTabWidget::currentChanged,
            this, [this](int) { updateMonitors(); });
    updateMonitors();

    setFixedSize(410, 446);
}

SoundDialog::~SoundDialog()
{
    stopMonitor(m_playback);
    stopMonitor(m_recording);
}

void SoundDialog::showTab(Tab tab)
{
    if (m_tabs)
        m_tabs->setCurrentIndex(tab);
}

// Playback / Recording tabs
QWidget *SoundDialog::buildDeviceTab(DeviceList &dl, bool sinks,
                                     const QString &prompt)
{
    dl.sinks = sinks;
    dl.devices = gatherDevices(sinks);
    dl.defaultName = defaultDevice(sinks);

    auto *page = new QWidget;
    page->setObjectName("devPage");
    page->setStyleSheet("#devPage { background: #FFFFFF; }");
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(14, 12, 14, 12);
    v->setSpacing(8);

    auto *promptLabel = new QLabel(prompt);
    {
        QFont f = promptLabel->font();
        f.setPointSize(9);
        promptLabel->setFont(f);
    }
    v->addWidget(promptLabel);

    // The bordered device list.
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet(
        "QScrollArea { border: 1px solid #A0A0A0; background: #FFFFFF; }");
    auto *container = new QWidget;
    container->setStyleSheet("background: #FFFFFF;");
    dl.box = new QVBoxLayout(container);
    dl.box->setContentsMargins(0, 0, 0, 0);
    dl.box->setSpacing(0);
    scroll->setWidget(container);
    v->addWidget(scroll, 1);

    // Buttons: Configure | (stretch) | Set Default ▾ | Properties.
    auto *btnRow = new QHBoxLayout;
    btnRow->setContentsMargins(0, 0, 0, 0);
    btnRow->setSpacing(8);

    dl.configureBtn = new QPushButton(QStringLiteral("Configure"));
    dl.configureBtn->setCursor(Qt::PointingHandCursor);
    btnRow->addWidget(dl.configureBtn);
    btnRow->addStretch(1);

    dl.setDefaultBtn = new QToolButton;
    dl.setDefaultBtn->setText(QStringLiteral("Set Default"));
    dl.setDefaultBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    dl.setDefaultBtn->setPopupMode(QToolButton::MenuButtonPopup);
    dl.setDefaultBtn->setCursor(Qt::PointingHandCursor);
    auto *menu = new QMenu(dl.setDefaultBtn);
    menu->addAction(QStringLiteral("Default Device"),
                    this, [this, &dl]() { applyDefault(dl); });
    menu->addAction(QStringLiteral("Default Communication Device"),
                    this, [this, &dl]() { applyDefault(dl); });
    dl.setDefaultBtn->setMenu(menu);
    connect(dl.setDefaultBtn, &QToolButton::clicked,
            this, [this, &dl]() { applyDefault(dl); });
    btnRow->addWidget(dl.setDefaultBtn);

    dl.propertiesBtn = new QPushButton(QStringLiteral("Properties"));
    dl.propertiesBtn->setCursor(Qt::PointingHandCursor);
    connect(dl.propertiesBtn, &QPushButton::clicked, this, [this, &dl]() {
        if (dl.selected < 0 || dl.selected >= dl.devices.size())
            return;
        const Device &d = dl.devices[dl.selected];
        QMessageBox::information(this, QStringLiteral("%1 Properties")
                                     .arg(d.description),
            QStringLiteral("%1\n%2\n\nVolume: %3%%4")
                .arg(d.description)
                .arg(d.cardName.isEmpty() ? QStringLiteral("Audio Device")
                                          : d.cardName)
                .arg(d.volumePercent)
                .arg(d.muted ? QStringLiteral("  (muted)") : QString()));
    });
    btnRow->addWidget(dl.propertiesBtn);

    v->addLayout(btnRow);

    rebuildRows(dl);

    // Pre-select the default device (or the first one).
    int initial = 0;
    for (int i = 0; i < dl.devices.size(); ++i)
        if (dl.devices[i].name == dl.defaultName)
            initial = i;
    if (!dl.devices.isEmpty())
        selectRow(dl, initial);
    else {
        dl.setDefaultBtn->setEnabled(false);
        dl.propertiesBtn->setEnabled(false);
        dl.configureBtn->setEnabled(false);
    }

    return page;
}

void SoundDialog::rebuildRows(DeviceList &dl)
{
    // The old meter widget is about to be deleted, so stop the capture feeding
    // it first; callers restart it via updateMonitors once rows are rebuilt.
    stopMonitor(dl);
    dl.activeMeter = nullptr;

    // Fully rebuild: clear the list (rows + trailing stretch), then re-add.
    while (QLayoutItem *item = dl.box->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    dl.rows.clear();
    dl.rowToIndex.clear();

    if (dl.devices.isEmpty()) {
        auto *empty = new QLabel(dl.sinks
            ? QStringLiteral("No playback devices were found.")
            : QStringLiteral("No recording devices were found."));
        empty->setStyleSheet("color: #555555; padding: 14px;");
        dl.box->addWidget(empty);
        dl.box->addStretch(1);
        return;
    }

    for (int i = 0; i < dl.devices.size(); ++i) {
        const Device &d = dl.devices[i];
        const bool isDefault = (d.name == dl.defaultName);

        auto *row = new QWidget;
        row->setObjectName("devRow");
        row->setAutoFillBackground(true);
        row->setCursor(Qt::PointingHandCursor);
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(8, 7, 8, 7);
        h->setSpacing(10);

        auto *icon = new QLabel;
        icon->setFixedSize(44, 44);
        icon->setPixmap(devicePixmap(
            deviceIcon(d.description + ' ' + d.name, dl.sinks), 44, isDefault));
        icon->setStyleSheet("background: transparent;");
        h->addWidget(icon, 0, Qt::AlignVCenter);

        auto *textCol = new QVBoxLayout;
        textCol->setContentsMargins(0, 0, 0, 0);
        textCol->setSpacing(0);

        auto *name = new QLabel(d.description.isEmpty() ? d.name : d.description);
        {
            QFont f = name->font();
            f.setPointSize(9);
            f.setBold(true);
            name->setFont(f);
        }
        name->setStyleSheet("color: #000000; background: transparent;");
        textCol->addWidget(name);

        QString cardLine = d.cardName;
        if (cardLine.isEmpty() || cardLine == d.description)
            cardLine = QStringLiteral("Audio Device");
        auto *card = new QLabel(cardLine);
        { QFont f = card->font(); f.setPointSize(9); card->setFont(f); }
        card->setStyleSheet("color: #333333; background: transparent;");
        textCol->addWidget(card);

        auto *status = new QLabel(isDefault ? QStringLiteral("Default Device")
                                            : QStringLiteral("Ready"));
        { QFont f = status->font(); f.setPointSize(9); status->setFont(f); }
        status->setStyleSheet("color: #333333; background: transparent;");
        textCol->addWidget(status);

        h->addLayout(textCol, 1);

        auto *meter = new LevelMeter;
        h->addWidget(meter, 0, Qt::AlignVCenter);
        if (isDefault)
            dl.activeMeter = meter;   // the device whose level we capture live

        row->installEventFilter(this);
        dl.rowToIndex.insert(row, i);
        dl.rows << row;
        dl.box->addWidget(row);
    }
    dl.box->addStretch(1);
}

void SoundDialog::selectRow(DeviceList &dl, int index)
{
    if (index < 0 || index >= dl.devices.size())
        return;
    dl.selected = index;

    for (auto it = dl.rowToIndex.constBegin(); it != dl.rowToIndex.constEnd();
         ++it) {
        auto *w = qobject_cast<QWidget *>(const_cast<QObject *>(it.key()));
        if (!w)
            continue;
        const bool sel = (it.value() == index);
        w->setStyleSheet(sel
            ? "#devRow { background: #CDE6FF; border: 1px solid #7DA2CE; }"
            : "#devRow { background: transparent; border: 1px solid transparent; }");
    }

    dl.propertiesBtn->setEnabled(true);
    dl.configureBtn->setEnabled(true);
    dl.setDefaultBtn->setEnabled(dl.devices[index].name != dl.defaultName);
}

void SoundDialog::applyDefault(DeviceList &dl)
{
    if (dl.selected < 0 || dl.selected >= dl.devices.size())
        return;
    runTool(QStringLiteral("pactl"),
            { dl.sinks ? QStringLiteral("set-default-sink")
                       : QStringLiteral("set-default-source"),
              dl.devices[dl.selected].name });

    dl.defaultName = defaultDevice(dl.sinks);
    rebuildRows(dl);
    selectRow(dl, dl.selected);
    updateMonitors();   // the meter now belongs to the new default device
}

// Live level metering
void SoundDialog::startMonitor(DeviceList &dl)
{
    if (dl.monitorProc || !dl.activeMeter || dl.defaultName.isEmpty())
        return;

    // Capture the sink's monitor (playback) or the source itself (recording) as
    // mono s16le and drive the meter from each chunk's peak amplitude.
    const QString dev = dl.sinks ? dl.defaultName + QStringLiteral(".monitor")
                                 : dl.defaultName;
    auto *proc = new QProcess(this);
    dl.monitorProc = proc;
    LevelMeter *meter = dl.activeMeter;
    connect(proc, &QProcess::readyReadStandardOutput, meter, [proc, meter]() {
        const QByteArray data = proc->readAllStandardOutput();
        const int count = data.size() / 2;
        const auto *samples =
            reinterpret_cast<const qint16 *>(data.constData());
        int peak = 0;
        for (int i = 0; i < count; ++i) {
            const int a = qAbs(int(samples[i]));
            if (a > peak)
                peak = a;
        }
        meter->setLevel(peak * 100 / 32768);
    });
    proc->start(QStringLiteral("parec"),
                { QStringLiteral("--format=s16le"), QStringLiteral("--channels=1"),
                  QStringLiteral("--rate=22050"), QStringLiteral("--latency-msec=60"),
                  QStringLiteral("-d"), dev });
}

void SoundDialog::stopMonitor(DeviceList &dl)
{
    if (!dl.monitorProc)
        return;
    dl.monitorProc->kill();
    dl.monitorProc->deleteLater();
    dl.monitorProc = nullptr;
    if (dl.activeMeter)
        dl.activeMeter->setLevel(0);
}

void SoundDialog::updateMonitors()
{
    const int cur = m_tabs->currentIndex();
    if (cur == TabPlayback)  startMonitor(m_playback);  else stopMonitor(m_playback);
    if (cur == TabRecording) startMonitor(m_recording); else stopMonitor(m_recording);
}

// Sounds tab
QWidget *SoundDialog::buildSoundsTab()
{
    auto *page = new QWidget;
    page->setObjectName("sndPage");
    page->setStyleSheet("#sndPage { background: #FFFFFF; }");
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(14, 14, 14, 12);
    v->setSpacing(6);

    QSettings s;

    auto para = [&](const QString &t) {
        auto *l = new QLabel(t);
        QFont f = l->font();
        f.setPointSize(9);
        l->setFont(f);
        l->setWordWrap(true);
        v->addWidget(l);
        return l;
    };

    para(Branding::brand("A sound theme is a set of sounds applied to events in "
                         "Linux. You can select an existing scheme or save one "
                         "you have modified."));

    auto *schemeLabel = new QLabel(QStringLiteral("Sound Scheme:"));
    { QFont f = schemeLabel->font(); f.setPointSize(9); schemeLabel->setFont(f); }
    v->addWidget(schemeLabel);

    auto *schemeRow = new QHBoxLayout;
    schemeRow->setSpacing(8);
    auto *scheme = new QComboBox;
    scheme->addItems({ Branding::brand("Linux Default"),
                       QStringLiteral("No Sounds") });
    scheme->setCurrentIndex(
        s.value(QStringLiteral("sound/scheme"), 0).toInt());
    scheme->setObjectName(QStringLiteral("soundScheme"));
    schemeRow->addWidget(scheme, 1);
    auto *saveAs = new QPushButton(QStringLiteral("Save As…"));
    schemeRow->addWidget(saveAs);
    auto *del = new QPushButton(QStringLiteral("Delete"));
    del->setEnabled(false);
    schemeRow->addWidget(del);
    v->addLayout(schemeRow);
    v->addSpacing(2);

    para(QStringLiteral("To change sounds, click a program event in the "
                        "following list and then select a sound to apply. You "
                        "can save the changes as a new sound scheme."));

    auto *eventsLabel = new QLabel(QStringLiteral("Program Events:"));
    { QFont f = eventsLabel->font(); f.setPointSize(9); eventsLabel->setFont(f); }
    v->addWidget(eventsLabel);

    auto *tree = new QTreeWidget;
    tree->setHeaderHidden(true);
    tree->setRootIsDecorated(true);
    tree->setStyleSheet("QTreeWidget { border: 1px solid #A0A0A0; }");
    auto *rootItem = new QTreeWidgetItem(tree, { Branding::os() });
    rootItem->setIcon(0, themeIcon({"computer", "start-here"}));
    const QStringList events = {
        "Asterisk", "Close Program", "Critical Battery Alarm", "Critical Stop",
        "Default Beep", "Desktop Mail Notification", "Device Connect",
        "Device Disconnect", "Device Failed to Connect", "Empty Recycle Bin",
        "Exclamation", "Low Battery Alarm", "New Fax Sound",
        "Notification", "System Notification", "Windows Logoff",
    };
    const QIcon evIcon = themeIcon({"audio-volume-high", "audio-speakers"});
    for (const QString &e : events) {
        auto *it = new QTreeWidgetItem(rootItem, { e });
        it->setIcon(0, evIcon);
    }
    rootItem->setExpanded(true);
    v->addWidget(tree, 1);

    auto *startup = new QCheckBox(Branding::brand("Play Linux Startup sound"));
    { QFont f = startup->font(); f.setPointSize(9); startup->setFont(f); }
    startup->setChecked(
        s.value(QStringLiteral("sound/startupSound"), true).toBool());
    startup->setObjectName(QStringLiteral("startupSound"));
    v->addWidget(startup);

    auto *soundsLabel = new QLabel(QStringLiteral("Sounds:"));
    { QFont f = soundsLabel->font(); f.setPointSize(9); soundsLabel->setFont(f); }
    v->addWidget(soundsLabel);

    auto *soundsRow = new QHBoxLayout;
    soundsRow->setSpacing(8);
    auto *soundsCombo = new QComboBox;
    soundsCombo->addItem(QStringLiteral("(None)"));
    soundsRow->addWidget(soundsCombo, 1);
    auto *test = new QPushButton(QStringLiteral("Test"));
    test->setIcon(themeIcon({"media-playback-start"}));
    connect(test, &QPushButton::clicked, this, []() {
        // Best-effort preview of a system sound.
        for (const char *f : { "/usr/share/sounds/freedesktop/stereo/bell.oga",
                               "/usr/share/sounds/freedesktop/stereo/message.oga",
                               "/usr/share/sounds/alsa/Front_Center.wav" }) {
            if (QFile::exists(QString::fromLatin1(f))) {
                QProcess::startDetached(QStringLiteral("paplay"),
                                        { QString::fromLatin1(f) });
                break;
            }
        }
    });
    soundsRow->addWidget(test);
    auto *browse = new QPushButton(QStringLiteral("Browse…"));
    soundsRow->addWidget(browse);
    v->addLayout(soundsRow);

    // Persist the cosmetic choices when the dialog is accepted.
    connect(this, &QDialog::accepted, this, [scheme, startup]() {
        QSettings st;
        st.setValue(QStringLiteral("sound/scheme"), scheme->currentIndex());
        st.setValue(QStringLiteral("sound/startupSound"), startup->isChecked());
    });

    return page;
}

// Communications tab
QWidget *SoundDialog::buildCommunicationsTab()
{
    auto *page = new QWidget;
    page->setObjectName("commPage");
    page->setStyleSheet("#commPage { background: #FFFFFF; }");
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(16, 16, 16, 12);
    v->setSpacing(0);

    auto *top = new QHBoxLayout;
    top->setSpacing(12);
    auto *icon = new QLabel;
    icon->setFixedSize(32, 32);
    icon->setPixmap(themeIcon({"call-start", "phone", "audio-input-microphone"})
                        .pixmap(32, 32));
    icon->setStyleSheet("background: transparent;");
    top->addWidget(icon, 0, Qt::AlignTop);
    auto *intro = new QLabel(Branding::brand(
        "Linux can automatically adjust the volume of different sounds when you "
        "are using your PC to place or receive telephone calls."));
    { QFont f = intro->font(); f.setPointSize(9); intro->setFont(f); }
    intro->setWordWrap(true);
    top->addWidget(intro, 1);
    v->addLayout(top);
    v->addSpacing(16);

    auto *prompt = new QLabel(
        Branding::brand("When Linux detects communications activity:"));
    { QFont f = prompt->font(); f.setPointSize(9); prompt->setFont(f); }
    v->addWidget(prompt);
    v->addSpacing(8);

    auto *group = new QButtonGroup(this);
    const QStringList options = {
        QStringLiteral("Mute all other sounds"),
        QStringLiteral("Reduce the volume of other sounds by 80%"),
        QStringLiteral("Reduce the volume of other sounds by 50%"),
        QStringLiteral("Do nothing"),
    };
    QSettings s;
    const int chosen = s.value(QStringLiteral("sound/communications"), 1).toInt();
    for (int i = 0; i < options.size(); ++i) {
        auto *r = new QRadioButton(options[i]);
        { QFont f = r->font(); f.setPointSize(9); r->setFont(f); }
        r->setChecked(i == chosen);
        group->addButton(r, i);
        auto *row = new QHBoxLayout;
        row->setContentsMargins(14, 0, 0, 0);
        row->addWidget(r);
        v->addLayout(row);
        v->addSpacing(6);
    }

    connect(this, &QDialog::accepted, this, [group]() {
        QSettings st;
        st.setValue(QStringLiteral("sound/communications"), group->checkedId());
    });

    v->addStretch(1);
    return page;
}

// Row selection
bool SoundDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress
        || event->type() == QEvent::MouseButtonDblClick) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() != Qt::LeftButton)
            return QDialog::eventFilter(watched, event);

        for (DeviceList *dl : { &m_playback, &m_recording }) {
            auto it = dl->rowToIndex.constFind(watched);
            if (it != dl->rowToIndex.constEnd()) {
                selectRow(*dl, it.value());
                if (event->type() == QEvent::MouseButtonDblClick)
                    applyDefault(*dl);
                return true;
            }
        }
    }
    return QDialog::eventFilter(watched, event);
}
