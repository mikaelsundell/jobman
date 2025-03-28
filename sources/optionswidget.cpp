// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#include "optionswidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPointer>
#include <QToolButton>
#include <QUuid>

class OptionsWidgetPrivate : public QObject {
    Q_OBJECT
public:
    OptionsWidgetPrivate();
    void init();
    void update();

public Q_SLOTS:
    void valueChanged(const QString& id, const QVariant& value);

public:
    QSharedPointer<Preset> preset;
    QPointer<OptionsWidget> widget;
};

OptionsWidgetPrivate::OptionsWidgetPrivate() {}

void
OptionsWidgetPrivate::init()
{
    preset.reset(new Preset());
}

void
OptionsWidgetPrivate::update()
{
    if (widget->layout()) {
        QLayoutItem* item;
        while ((item = widget->layout()->takeAt(0)) != nullptr) {
            if (QWidget* w = item->widget()) {
                w->deleteLater();
            }
            delete item;
        }
        delete widget->layout();
    }
    QGridLayout* layout = new QGridLayout(widget.data());
    int row = 0;
    QMargins margins(0, 0, 0, 0);
    for (QSharedPointer<Option> option : preset->options()) {
        QWidget* labelwidget = new QWidget(widget.data());
        QHBoxLayout* labellayout = new QHBoxLayout(labelwidget);
        labellayout->setContentsMargins(margins);
        labellayout->setSpacing(4);

        QLabel* label = new QLabel(option->name, labelwidget);
        QFont font;
        font.setPointSize(10);
        label->setFont(font);
        labellayout->addWidget(label);

        if (!option->description.isEmpty()) {
            QToolButton* tooltip = new QToolButton(labelwidget);
            tooltip->setObjectName("tooltip");
            tooltip->setMaximumSize(QSize(16, 16));
            QIcon icon;
            icon.addFile(QString::fromUtf8(":/icons/resources/Tooltip.png"), QSize(), QIcon::Mode::Normal,
                         QIcon::State::Off);
            tooltip->setIcon(icon);
            tooltip->setToolButtonStyle(Qt::ToolButtonStyle::ToolButtonIconOnly);
            tooltip->setToolTip(option->description);
            labellayout->addWidget(tooltip);
        }
        labellayout->addStretch();
        layout->addWidget(labelwidget, row, 0, Qt::AlignTop);

        if (option->type.toLower() == "checkbox") {
            QWidget* optionwidget = new QWidget(widget.data());
            QVBoxLayout* optionlayout = new QVBoxLayout(optionwidget);
            optionlayout->setContentsMargins(margins);

            QCheckBox* checkbox = new QCheckBox(optionwidget);
            checkbox->setChecked(option->value.toBool());

            connect(checkbox, &QCheckBox::toggled, this,
                    [this, option](bool checked) { valueChanged(option->id, checked); });

            optionlayout->addWidget(checkbox);

            if (!option->toggle.isEmpty()) {
                QCheckBox* togglebox = new QCheckBox(option->toggle, optionwidget);
                togglebox->setChecked(option->enabled);
                checkbox->setEnabled(option->enabled);
                connect(togglebox, &QCheckBox::toggled, this, [checkbox, option](bool checked) {
                    checkbox->setEnabled(checked);
                    option->enabled = checked;
                });
                optionlayout->addWidget(togglebox);
            }

            layout->addWidget(optionwidget, row, 1, Qt::AlignTop);
        }
        if (option->type.toLower() == "double") {
            QWidget* optionwidget = new QWidget(widget.data());
            QVBoxLayout* optionlayout = new QVBoxLayout(optionwidget);
            optionlayout->setContentsMargins(margins);

            QLineEdit* lineedit = new QLineEdit(option->value.toString(), optionwidget);
            QDoubleValidator* validator = new QDoubleValidator(option->minimum.toDouble(), option->maximum.toDouble(),
                                                               6, lineedit);
            validator->setNotation(QDoubleValidator::StandardNotation);
            validator->setLocale(QLocale::C);  // use '.' instead of ','
            lineedit->setValidator(validator);
            lineedit->setFont(font);

            connect(lineedit, &QLineEdit::textEdited, this, [this, lineedit, option]() {
                QString text = lineedit->text();
                if (text.contains(',')) {  // needed, will still allow ','
                    text.replace(',', '.');
                    lineedit->setText(text);
                }
                if (!(!text.isEmpty() && text.back() == QChar('.'))) {
                    bool valid;
                    double value = text.toDouble(&valid);
                    if (valid) {
                        double minimum = option->minimum.toDouble();
                        double maximum = option->maximum.toDouble();
                        if (value < minimum) {
                            value = minimum;
                        }
                        else if (value > maximum) {
                            value = maximum;
                        }
                        lineedit->setText(QString::number(value));
                        valueChanged(option->id, value);
                    }
                }
            });

            connect(lineedit, &QLineEdit::editingFinished, this, [lineedit]() {
                QString text = lineedit->text();
                if (text.endsWith('.')) {
                    text.chop(1);
                    lineedit->setText(text);
                }
            });

            optionlayout->addWidget(lineedit);

            if (!option->toggle.isEmpty()) {
                QCheckBox* togglebox = new QCheckBox(option->toggle, optionwidget);
                togglebox->setChecked(option->enabled);
                lineedit->setEnabled(option->enabled);
                connect(togglebox, &QCheckBox::toggled, this, [lineedit, option](bool checked) {
                    lineedit->setEnabled(checked);
                    option->enabled = checked;
                });
                optionlayout->addWidget(togglebox);
            }

            layout->addWidget(optionwidget, row, 1, Qt::AlignTop);
        }
        else if (option->type.toLower() == "dropdown") {
            QWidget* optionwidget = new QWidget(widget.data());
            QVBoxLayout* optionlayout = new QVBoxLayout(optionwidget);
            optionlayout->setContentsMargins(margins);

            QComboBox* combobox = new QComboBox(optionwidget);
            int currentindex = -1;

            for (int i = 0; i < option->options.size(); ++i) {
                const QPair<QString, QVariant>& pair = option->options[i];
                combobox->addItem(pair.first, pair.second);
                if (pair.second == option->value) {
                    currentindex = i;
                }
            }
            if (currentindex != -1) {
                combobox->setCurrentIndex(currentindex);
            }

            connect(combobox, &QComboBox::currentIndexChanged, this,
                    [this, option, combobox](int index) { valueChanged(option->id, combobox->itemData(index)); });

            optionlayout->addWidget(combobox);

            if (!option->toggle.isEmpty()) {
                QCheckBox* togglebox = new QCheckBox(option->toggle, optionwidget);
                togglebox->setChecked(option->enabled);
                combobox->setEnabled(option->enabled);
                connect(togglebox, &QCheckBox::toggled, this, [combobox, option](bool checked) {
                    combobox->setEnabled(checked);
                    option->enabled = checked;
                });
                optionlayout->addWidget(togglebox);
            }
            layout->addWidget(optionwidget, row, 1, Qt::AlignTop);
        }
        else if (option->type.toLower() == "doubleslider") {
            QWidget* optionwidget = new QWidget(widget.data());
            QVBoxLayout* optionlayout = new QVBoxLayout(optionwidget);
            optionlayout->setContentsMargins(margins);

            QHBoxLayout* sliderlayout = new QHBoxLayout();
            QSlider* slider = new QSlider(Qt::Horizontal, widget.data());

            double minValue = option->minimum.toDouble();
            double maxValue = option->maximum.toDouble();
            int precision = 10;

            slider->setRange(minValue * precision, maxValue * precision);
            slider->setValue(option->value.toDouble() * precision);

            QLabel* sliderlabel = new QLabel(QString::number(slider->value() / (double)precision, 'f', 1),
                                             widget.data());
            sliderlabel->setFont(font);
            sliderlabel->setFixedWidth(40);
            sliderlabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

            connect(slider, &QSlider::valueChanged, this, [this, option, sliderlabel, precision](int value) {
                double doublevalue = value / (double)precision;
                sliderlabel->setText(QString::number(doublevalue, 'f', 1));
                valueChanged(option->id, doublevalue);
            });

            sliderlayout->addWidget(slider);
            sliderlayout->addWidget(sliderlabel);
            optionlayout->addLayout(sliderlayout);

            if (!option->toggle.isEmpty()) {
                QCheckBox* togglebox = new QCheckBox(option->toggle, optionwidget);
                togglebox->setChecked(option->enabled);
                slider->setEnabled(option->enabled);
                sliderlabel->setEnabled(option->enabled);
                connect(togglebox, &QCheckBox::toggled, this, [slider, sliderlabel, option](bool checked) {
                    slider->setEnabled(checked);
                    sliderlabel->setEnabled(checked);
                    option->enabled = checked;
                });
                optionlayout->addWidget(togglebox);
            }

            layout->addWidget(optionwidget, row, 1, Qt::AlignTop);
        }
        else if (option->type.toLower() == "file") {
            QWidget* optionwidget = new QWidget(widget.data());
            QVBoxLayout* optionlayout = new QVBoxLayout(optionwidget);
            optionlayout->setContentsMargins(margins);

            QHBoxLayout* filelayout = new QHBoxLayout();
            QLineEdit* lineedit = new QLineEdit(option->value.toString(), optionwidget);
            lineedit->setReadOnly(true);
            filelayout->addWidget(lineedit);

            QIcon icon;
            icon.addFile(QString::fromUtf8(":/icons/resources/Folder.png"), QSize(), QIcon::Normal, QIcon::Off);
            QToolButton* button = new QToolButton(optionwidget);
            button->setIcon(icon);
            filelayout->addWidget(button);

            connect(button, &QToolButton::clicked, this, [this, lineedit, option]() {
                QString filepath = QFileDialog::getOpenFileName(nullptr, tr("Select File"));
                if (!filepath.isEmpty()) {
                    lineedit->setText(filepath);
                    valueChanged(option->id, filepath);
                }
            });
            connect(lineedit, &QLineEdit::textChanged, this,
                    [this, option](const QString& text) { valueChanged(option->id, text); });

            optionlayout->addLayout(filelayout);

            if (!option->toggle.isEmpty()) {
                QCheckBox* togglebox = new QCheckBox(option->toggle, optionwidget);
                togglebox->setChecked(option->enabled);
                lineedit->setEnabled(option->enabled);
                button->setEnabled(option->enabled);
                connect(togglebox, &QCheckBox::toggled, this, [lineedit, button, option](bool checked) {
                    lineedit->setEnabled(checked);
                    button->setEnabled(checked);
                    option->enabled = checked;
                });
                optionlayout->addWidget(togglebox);
            }

            layout->addWidget(optionwidget, row, 1, Qt::AlignTop);
        }
        else if (option->type.toLower() == "int") {
            QWidget* optionwidget = new QWidget(widget.data());
            QVBoxLayout* optionlayout = new QVBoxLayout(optionwidget);
            optionlayout->setContentsMargins(margins);

            int value = static_cast<int>(option->value.toDouble());  // needed toInt() does not handle decimals
            QLineEdit* lineedit = new QLineEdit(QString::number(value), optionwidget);
            QIntValidator* validator = new QIntValidator(option->minimum.toInt(), option->maximum.toInt(), lineedit);
            lineedit->setValidator(validator);
            lineedit->setFont(font);

            connect(lineedit, &QLineEdit::textEdited, this, [this, lineedit, option]() {
                QString text = lineedit->text();
                bool valid;
                int value = text.toInt(&valid);
                if (valid) {
                    int minimum = option->minimum.toInt();
                    int maximum = option->maximum.toInt();
                    if (value < minimum) {
                        value = minimum;
                    }
                    else if (value > maximum) {
                        value = maximum;
                    }
                    lineedit->setText(QString::number(value));
                    valueChanged(option->id, value);
                }
            });

            optionlayout->addWidget(lineedit);

            if (!option->toggle.isEmpty()) {
                QCheckBox* togglebox = new QCheckBox(option->toggle, optionwidget);
                togglebox->setChecked(option->enabled);
                lineedit->setEnabled(option->enabled);
                connect(togglebox, &QCheckBox::toggled, this, [lineedit, option](bool checked) {
                    lineedit->setEnabled(checked);
                    option->enabled = checked;
                });
                optionlayout->addWidget(togglebox);
            }

            layout->addWidget(optionwidget, row, 1, Qt::AlignTop);
        }
        else if (option->type.toLower() == "intslider") {
            QWidget* optionwidget = new QWidget(widget.data());
            QVBoxLayout* optionlayout = new QVBoxLayout(optionwidget);
            optionlayout->setContentsMargins(margins);

            QHBoxLayout* sliderlayout = new QHBoxLayout();
            QSlider* slider = new QSlider(Qt::Horizontal, widget.data());
            slider->setRange(option->minimum.toInt(), option->maximum.toInt());
            slider->setValue(option->value.toInt());

            QLabel* sliderlabel = new QLabel(QString::number(slider->value()), widget.data());
            sliderlabel->setFont(font);
            sliderlabel->setFixedWidth(20);
            sliderlabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

            connect(slider, &QSlider::valueChanged, this, [this, option, sliderlabel](int value) {
                sliderlabel->setText(QString::number(value));
                valueChanged(option->id, value);
            });
            sliderlayout->addWidget(slider);
            sliderlayout->addWidget(sliderlabel);

            optionlayout->addLayout(sliderlayout);

            if (!option->toggle.isEmpty()) {
                QCheckBox* togglebox = new QCheckBox(option->toggle, optionwidget);
                togglebox->setChecked(option->enabled);
                slider->setEnabled(option->enabled);
                sliderlabel->setEnabled(option->enabled);
                connect(togglebox, &QCheckBox::toggled, this, [slider, sliderlabel, option](bool checked) {
                    slider->setEnabled(checked);
                    sliderlabel->setEnabled(checked);
                    option->enabled = checked;
                });
                optionlayout->addWidget(togglebox);
            }

            layout->addWidget(optionwidget, row, 1, Qt::AlignTop);
        }
        else if (option->type.toLower() == "text") {
            QWidget* optionwidget = new QWidget(widget.data());
            QVBoxLayout* optionlayout = new QVBoxLayout(optionwidget);
            optionlayout->setContentsMargins(margins);

            QLineEdit* lineedit = new QLineEdit(option->value.toString(), widget.data());
            lineedit->setFont(font);
            connect(lineedit, &QLineEdit::textChanged, this,
                    [this, option](const QString& text) { valueChanged(option->id, text); });

            optionlayout->addWidget(lineedit);

            if (!option->toggle.isEmpty()) {
                QCheckBox* togglebox = new QCheckBox(option->toggle, optionwidget);
                togglebox->setChecked(option->enabled);
                lineedit->setEnabled(option->enabled);
                connect(togglebox, &QCheckBox::toggled, this, [lineedit, option](bool checked) {
                    lineedit->setEnabled(checked);
                    option->enabled = checked;
                });
                optionlayout->addWidget(togglebox);
            }
            layout->addWidget(optionwidget, row, 1, Qt::AlignTop);
        }
        row++;
    }
    layout->setColumnStretch(0, 1);
    layout->setColumnStretch(1, 3);
    layout->addItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding), row, 0, 1, 2);
    widget->setLayout(layout);
}

void
OptionsWidgetPrivate::valueChanged(const QString& id, const QVariant& value)
{
    for (QSharedPointer<Option> option : preset->options()) {
        if (option->id == id) {
            option->value = value;
            break;
        }
    }
}

#include "optionswidget.moc"

OptionsWidget::OptionsWidget(QWidget* parent)
    : QWidget(parent)
    , p(new OptionsWidgetPrivate())
{
    p->widget = this;
    p->init();
}

OptionsWidget::~OptionsWidget() {}

void
OptionsWidget::update(QSharedPointer<Preset> preset)
{
    if (p->preset->uuid() != preset->uuid()) {
        p->preset = preset;
        p->update();
    }
}
