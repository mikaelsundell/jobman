// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#include "options.h"
#include "question.h"

#include <QComboBox>
#include <QCheckBox>
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
        void defaults();

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
    connect(ui->defaults, &QPushButton::pressed, this, &OptionsPrivate::defaults);
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
    QGridLayout* layout = new QGridLayout(containerwidget);
    int row = 0;
    for (Option* option : preset->options()) {
        QLabel* label = new QLabel(option->name, containerwidget);
        QFont font;
        font.setPointSize(10);
        label->setFont(font);
        layout->addWidget(label, row, 0);

        if (option->type == "Checkbox") {
            QCheckBox* checkbox = new QCheckBox(containerwidget);
            checkbox->setChecked(option->value.toBool());
            connect(checkbox, &QCheckBox::toggled, this, [this, option](bool checked) {
                valueChanged(option->id, checked);
            });
            optionwidgets[option->id] = checkbox;
            layout->addWidget(checkbox, row, 1);
        }
        else if (option->type == "Double") {
            QLineEdit* lineedit = new QLineEdit(option->value.toString(), containerwidget);
            QDoubleValidator* validator = new QDoubleValidator(option->minimum.toDouble(), option->maximum.toDouble(), 3, lineedit);
            validator->setNotation(QDoubleValidator::StandardNotation);
            validator->setLocale(QLocale::C); // use '.' instead of ','
            lineedit->setValidator(validator);
            lineedit->setFont(font);
            connect(lineedit, &QLineEdit::textEdited, this, [lineedit]() {
                QString text = lineedit->text();
                if (text.contains(',')) { // needed, will still allow ','
                    text.replace(',', '.');
                    lineedit->setText(text);
                }
            });
            connect(lineedit, &QLineEdit::textChanged, this, [this, option](const QString &text) {
                bool valid;
                double value = text.toDouble(&valid);
                if (valid) {
                    valueChanged(option->id, value);
                }
            });
            optionwidgets[option->id] = lineedit;
            layout->addWidget(lineedit, row, 1);
        }
        else if (option->type == "Dropdown") {
            QComboBox* combobox = new QComboBox(containerwidget);
            int currentindex = -1;
            for (int i = 0; i < option->options.size(); ++i) {
                const auto &optPair = option->options[i];
                combobox->addItem(optPair.first, optPair.second);
                if (optPair.second == option->value) {
                    currentindex = i;
                }
            }
            if (currentindex != -1) {
                combobox->setCurrentIndex(currentindex);
            }
            connect(combobox, &QComboBox::currentIndexChanged, this, [this, option, combobox](int index) {
                valueChanged(option->id, combobox->itemData(index));
            });
            optionwidgets[option->id] = combobox;
            layout->addWidget(combobox, row, 1);
        }
        else if (option->type == "File") {
            QWidget* filewidget = new QWidget(containerwidget);
            QHBoxLayout* filelayout = new QHBoxLayout(filewidget);
            filelayout->setContentsMargins(0, 0, 0, 0);

            QLineEdit* lineedit = new QLineEdit(option->value.toString(), filewidget);
            lineedit->setReadOnly(true);
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
                    valueChanged(option->id, filepath);
                }
            });
            connect(lineedit, &QLineEdit::textChanged, this, [this, option](const QString &text) {
                valueChanged(option->id, text);
            });
            optionwidgets[option->id] = lineedit;
            layout->addWidget(filewidget, row, 1);
        }
        else if (option->type == "Slider") {
            QHBoxLayout* sliderlayout = new QHBoxLayout();
            QSlider* slider = new QSlider(Qt::Horizontal, containerwidget);
            slider->setRange(option->minimum.toInt(), option->maximum.toInt());
            slider->setValue(option->value.toInt());

            QLabel* sliderValueLabel = new QLabel(QString::number(slider->value()), containerwidget);
            sliderValueLabel->setFont(font);
            sliderValueLabel->setFixedWidth(20);
            sliderValueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

            connect(slider, &QSlider::valueChanged, this, [this, option, sliderValueLabel](int value) {
                sliderValueLabel->setText(QString::number(value));
                valueChanged(option->id, value);
            });
            optionwidgets[option->id] = slider;
            sliderlayout->addWidget(slider);
            sliderlayout->addWidget(sliderValueLabel);
            layout->addLayout(sliderlayout, row, 1);
        }
        else if (option->type == "Text") {
            QLineEdit* lineedit = new QLineEdit(option->value.toString(), containerwidget);
            lineedit->setFont(font);
            connect(lineedit, &QLineEdit::textChanged, this, [this, option](const QString &text) {
                valueChanged(option->id, text);
            });
            optionwidgets[option->id] = lineedit;
            layout->addWidget(lineedit, row, 1);
        }
        row++;
    }
    layout->setColumnStretch(0, 1);
    layout->setColumnStretch(1, 3);
    layout->addItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding), row, 0, 1, 2);

    containerwidget->setLayout(layout);
    ui->scrollarea->setWidget(containerwidget);
    ui->scrollarea->setWidgetResizable(true);
}

void
OptionsPrivate::valueChanged(const QString& id, const QVariant& value)
{
    QList<Option*> options = preset->options();
    for (Option* option : options) {
        if (option->id == id) {
            option->value = value;
            break;
        }
    }
}

void
OptionsPrivate::close()
{
    dialog->close();
}

void
OptionsPrivate::defaults()
{
    if (Question::askQuestion(dialog.data(),
        "All values will be reset to their default settings.\n"
        "Do you want to continue?"
    )) {
        for (Option* option : preset->options()) {
            option->value = option->defaultvalue;
        }
        update();
    }
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
