// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#include "options.h"

#include <QComboBox>
#include <QFileDialog>
#include <QLineEdit>
#include <QPointer>
#include <QSlider>
#include <QSettings>
#include <QStandardPaths>
#include <QToolButton>
#include <QUuid>
#include <QDebug>

// generated files
#include "ui_options.h"

class OptionsPrivate : public QObject
{
    Q_OBJECT
    public:
    OptionsPrivate();
        void init();
        void update();

    public Q_SLOTS:
        void valueChanged(const QString& name, const QVariant& value);
        void close();

    public:
        QMap<QString, QWidget*> optionwidgets;
        QSharedPointer<Preset> preset;
        QPointer<Options> dialog;
        QScopedPointer<Ui_Options> ui;
};

OptionsPrivate::OptionsPrivate()
{
}

void
OptionsPrivate::init()
{
    // ui
    ui.reset(new Ui_Options());
    ui->setupUi(dialog);
    // event filter
    dialog->installEventFilter(this);
    // preset
    preset.reset(new Preset());
    // connect
    connect(ui->close, &QPushButton::pressed, this, &OptionsPrivate::close);
}

void
OptionsPrivate::update() {
    ui->name->setText(preset->name());
    if (ui->scrollarea->widget()) {
        delete ui->scrollarea->widget();
    }
    optionwidgets.clear();
    QWidget* containerwidget = new QWidget();
    containerwidget->setObjectName("optionswidget");
    QVBoxLayout* layout = new QVBoxLayout(containerwidget);
    
    QSettings settings(MACOSX_BUNDLE_GUI_IDENTIFIER, "Jobman");
    settings.beginGroup(preset->id());
    for (Option &option : preset->options()) {
        QVariant savedvalue = settings.value(option.name, option.value);
        option.value = savedvalue;
        QLabel* label = new QLabel(option.name, containerwidget);
        QFont font;
        font.setPointSize(12);
        font.setBold(true);
        label->setFont(font);
        layout->addWidget(label);
        if (option.type == "Slider") {
            QSlider *slider = new QSlider(Qt::Horizontal, containerwidget);
            slider->setRange(option.minimum.toInt(), option.maximum.toInt());
            slider->setValue(option.value.toInt());
            connect(slider, &QSlider::valueChanged, this, [this, option](int value) {
                valueChanged(option.name, value);
            });
            optionwidgets[option.name] = slider;
            layout->addWidget(slider);
        }
        else if (option.type == "Dropdown") {
            QComboBox* combobox = new QComboBox(containerwidget);
            int currentindex = -1;
            for (int i = 0; i < option.options.size(); ++i) {
                const auto &optPair = option.options[i];
                combobox->addItem(optPair.first, optPair.second);
                if (optPair.second == option.value) {
                    currentindex = i;
                }
            }
            if (currentindex != -1) {
                combobox->setCurrentIndex(currentindex);
            }
            connect(combobox, &QComboBox::currentIndexChanged, this, [this, option, combobox](int index) {
                valueChanged(option.name, combobox->itemData(index));  // use itemData to get value
            });
            optionwidgets[option.name] = combobox;
            layout->addWidget(combobox);

        }
        else if (option.type == "Text") {
            QLineEdit* lineedit = new QLineEdit(option.value.toString(), containerwidget);
            connect(lineedit, &QLineEdit::textChanged, this, [this, option](const QString &text) {
                valueChanged(option.name, text);
            });
            optionwidgets[option.name] = lineedit;
            layout->addWidget(lineedit);
        }
        else if (option.type == "File") {
            QWidget* filewidget = new QWidget(containerwidget);
            QHBoxLayout* filelayout = new QHBoxLayout(filewidget);
            filelayout->setContentsMargins(0, 0, 0, 0);
            QLineEdit* lineedit = new QLineEdit(option.value.toString(), filewidget);
            filelayout->addWidget(lineedit);
            QIcon icon;
            icon.addFile(QString::fromUtf8(":/icons/resources/Folder.png"), QSize(), QIcon::Normal, QIcon::Off);
            QToolButton* button = new QToolButton(filewidget);
            button->setIcon(icon);
            filelayout->addWidget(button);

            connect(button, &QToolButton::clicked, this, [this, lineedit, option]() {
                QString filepath = QFileDialog::getOpenFileName(nullptr, tr("Select File"));
                if (!filepath.isEmpty()) {
                    lineedit->setText(filepath);
                    valueChanged(option.name, filepath);
                }
            });
            connect(lineedit, &QLineEdit::textChanged, this, [this, option](const QString &text) {
                valueChanged(option.name, text);
            });
            optionwidgets[option.name] = lineedit;
            layout->addWidget(filewidget);
        }
    }
    layout->addStretch();
    containerwidget->setLayout(layout);
    ui->scrollarea->setWidget(containerwidget);
    ui->scrollarea->setWidgetResizable(true);
    settings.endGroup();
}

void
OptionsPrivate::valueChanged(const QString& name, const QVariant& value)
{
    for (Option &option : preset->options()) {
        if (option.name == name) {
            option.value = value;
            QSettings settings(MACOSX_BUNDLE_GUI_IDENTIFIER, "Jobman");
            settings.beginGroup(preset->id());
            settings.setValue(name, value);
            settings.endGroup();
            break;
        }
    }
}

void
OptionsPrivate::close()
{
    dialog->close();
}

#include "options.moc"

Options::Options(QWidget* parent)
: QDialog(parent)
, p(new OptionsPrivate())
{
    p->dialog = this;
    p->init();
}

Options::~Options()
{
}

void
Options::update(QSharedPointer<Preset> preset)
{
    if (p->preset->uuid() != preset->uuid()) {
        p->preset = preset;
        p->update();
    }
}
