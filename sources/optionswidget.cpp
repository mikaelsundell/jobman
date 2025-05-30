// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#include "optionswidget.h"
#include "utils.h"

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
    QLineEdit* doubleedit(const QSharedPointer<Option>& option, QWidget* parent = nullptr);
    QLineEdit* intedit(const QSharedPointer<Option>& option, QWidget* parent = nullptr);

public Q_SLOTS:
    void valueChanged(const QString& id, const QVariant& value);

public:
    QFont font;
    int editprecision;
    int labelprecision;
    QSharedPointer<Preset> preset;
    QPointer<OptionsWidget> widget;
};

OptionsWidgetPrivate::OptionsWidgetPrivate()
    : editprecision(6)
    , labelprecision(3)
{}

void
OptionsWidgetPrivate::init()
{
    preset.reset(new Preset());
    // font
    font.setPointSize(10);
}

void
OptionsWidgetPrivate::update()
{
    QGridLayout* layout = new QGridLayout(widget.data());
    int row = 0;
    QMargins margins(0, 0, 0, 0);
    for (QSharedPointer<Option> option : preset->options()) {
        QWidget* labelwidget = new QWidget(widget.data());
        QHBoxLayout* labellayout = new QHBoxLayout(labelwidget);
        labellayout->setContentsMargins(margins);
        labellayout->setSpacing(4);

        QLabel* label = new QLabel(option->name, labelwidget);
        label->setFont(font);
        labellayout->addWidget(label);
        int rowheight = 30;

        if (!option->description.isEmpty()) {
            QToolButton* tooltip = new QToolButton(labelwidget);
            tooltip->setObjectName("tooltip");
            tooltip->setMaximumSize(QSize(16, 16));
            QIcon icon;
            icon.addFile(QString::fromUtf8(":/icons/resources/Tooltip.png"), QSize(), QIcon::Mode::Normal,
                         QIcon::State::Off);
            tooltip->setIcon(icon);
            tooltip->setToolButtonStyle(Qt::ToolButtonStyle::ToolButtonIconOnly);

            QVariant defaultvalue = option->defaultvalue;
            QString tooltiptext = option->description;
            if (defaultvalue.isValid() && !defaultvalue.isNull() && defaultvalue.toString().size()) {
                QString formatted = (defaultvalue.typeId() == QMetaType::Double)
                                        ? QString::number(defaultvalue.toDouble(), 'f', 3)
                                        : defaultvalue.toString();
                tooltiptext += QString(" (default: %1)").arg(formatted);
            }
            tooltip->setToolTip(tooltiptext);
            labellayout->addWidget(tooltip);
        }
        labellayout->addStretch();
        layout->addWidget(labelwidget, row, 0, Qt::AlignTop);
        layout->setRowMinimumHeight(layout->rowCount() - 1, rowheight);

        if (option->type.toLower() == "checkbox") {
            QWidget* optionwidget = new QWidget(widget.data());
            QVBoxLayout* optionlayout = new QVBoxLayout(optionwidget);
            optionlayout->setContentsMargins(margins);

            QCheckBox* checkbox = new QCheckBox(optionwidget);
            checkbox->setChecked(option->value.toBool());
            checkbox->setFont(font);

            connect(checkbox, &QCheckBox::toggled, this,
                    [this, option](bool checked) { valueChanged(option->id, checked); });

            optionlayout->addWidget(checkbox);

            if (!option->toggle.isEmpty()) {
                QCheckBox* togglebox = new QCheckBox(option->toggle, optionwidget);
                togglebox->setFont(font);
                togglebox->setChecked(option->enabled);
                checkbox->setEnabled(option->enabled);
                connect(togglebox, &QCheckBox::toggled, this, [checkbox, option](bool checked) {
                    checkbox->setEnabled(checked);
                    option->enabled = checked;
                });
                optionlayout->addWidget(togglebox);
            }

            layout->addWidget(optionwidget, row, 1, Qt::AlignTop);
            layout->setRowMinimumHeight(layout->rowCount() - 1, rowheight);
        }
        else if (option->type.toLower() == "double") {
            QWidget* optionwidget = new QWidget(widget.data());
            QVBoxLayout* optionlayout = new QVBoxLayout(optionwidget);
            optionlayout->setContentsMargins(margins);

            QLineEdit* lineedit = doubleedit(option, optionwidget);
            optionlayout->addWidget(lineedit);

            if (!option->toggle.isEmpty()) {
                QCheckBox* togglebox = new QCheckBox(option->toggle, optionwidget);
                togglebox->setFont(font);
                togglebox->setChecked(option->enabled);
                lineedit->setEnabled(option->enabled);
                connect(togglebox, &QCheckBox::toggled, this, [lineedit, option](bool checked) {
                    lineedit->setEnabled(checked);
                    option->enabled = checked;
                });
                optionlayout->addWidget(togglebox);
            }

            layout->addWidget(optionwidget, row, 1, Qt::AlignTop);
            layout->setRowMinimumHeight(layout->rowCount() - 1, rowheight);
        }
        else if (option->type.toLower() == "dropdown") {
            QWidget* optionwidget = new QWidget(widget.data());
            QVBoxLayout* optionlayout = new QVBoxLayout(optionwidget);
            optionlayout->setContentsMargins(margins);

            QComboBox* combobox = new QComboBox(optionwidget);
            combobox->setFont(font);
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
                togglebox->setFont(font);
                togglebox->setChecked(option->enabled);
                combobox->setEnabled(option->enabled);
                connect(togglebox, &QCheckBox::toggled, this, [combobox, option](bool checked) {
                    combobox->setEnabled(checked);
                    option->enabled = checked;
                });
                optionlayout->addWidget(togglebox);
            }
            layout->addWidget(optionwidget, row, 1, Qt::AlignTop);
            layout->setRowMinimumHeight(layout->rowCount() - 1, rowheight);
        }
        else if (option->type.toLower() == "doubleslider") {
            QWidget* optionwidget = new QWidget(widget.data());
            QVBoxLayout* optionlayout = new QVBoxLayout(optionwidget);
            optionlayout->setContentsMargins(margins);

            QHBoxLayout* sliderlayout = new QHBoxLayout();
            QSlider* slider = new QSlider(Qt::Horizontal, widget.data());

            double minvalue = option->minimum.toDouble();
            double maxvalue = option->maximum.toDouble();
            double value = option->value.toDouble();

            int steps = 1000;
            int sliderpos = static_cast<int>(((value - minvalue) / (maxvalue - minvalue)) * steps);

            slider->setRange(0, steps);
            slider->setValue(sliderpos);

            QLineEdit* lineedit = doubleedit(option, widget.data());
            lineedit->setFixedWidth(60);

            connect(slider, &QSlider::valueChanged, this, [=]() {
                double mappedValue = minvalue + ((maxvalue - minvalue) * slider->value() / steps);
                lineedit->setText(utils::formatDouble(mappedValue));
                valueChanged(option->id, mappedValue);
            });

            connect(lineedit, &QLineEdit::editingFinished, this, [=]() {
                bool valid;
                double typedValue = lineedit->text().toDouble(&valid);
                if (valid) {
                    typedValue = std::clamp(typedValue, minvalue, maxvalue);
                    int newSliderPos = static_cast<int>(((typedValue - minvalue) / (maxvalue - minvalue)) * steps);
                    slider->setValue(newSliderPos);
                }
            });

            sliderlayout->addWidget(slider);
            sliderlayout->addWidget(lineedit);
            optionlayout->addLayout(sliderlayout);

            if (!option->toggle.isEmpty()) {
                QCheckBox* togglebox = new QCheckBox(option->toggle, optionwidget);
                togglebox->setFont(font);
                togglebox->setChecked(option->enabled);
                slider->setEnabled(option->enabled);
                lineedit->setEnabled(option->enabled);
                connect(togglebox, &QCheckBox::toggled, this, [=](bool checked) {
                    slider->setEnabled(checked);
                    lineedit->setEnabled(checked);
                    option->enabled = checked;
                });
                optionlayout->addWidget(togglebox);
            }

            layout->addWidget(optionwidget, row, 1, Qt::AlignTop);
            layout->setRowMinimumHeight(layout->rowCount() - 1, rowheight);
        }
        else if (option->type.toLower() == "openfile" || option->type.toLower() == "savefile") {
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
                QString filepath;
                if (option->type.toLower() == "openfile") {
                    filepath = QFileDialog::getOpenFileName(nullptr, tr("Open File"));
                }
                else {
                    filepath = QFileDialog::getSaveFileName(nullptr, tr("Save File"));
                }
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
                togglebox->setFont(font);
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
            layout->setRowMinimumHeight(layout->rowCount() - 1, rowheight);
        }
        else if (option->type.toLower() == "int") {
            QWidget* optionwidget = new QWidget(widget.data());
            QVBoxLayout* optionlayout = new QVBoxLayout(optionwidget);
            optionlayout->setContentsMargins(margins);

            QLineEdit* lineedit = intedit(option, optionwidget);
            optionlayout->addWidget(lineedit);

            if (!option->toggle.isEmpty()) {
                QCheckBox* togglebox = new QCheckBox(option->toggle, optionwidget);
                togglebox->setFont(font);
                togglebox->setChecked(option->enabled);
                lineedit->setEnabled(option->enabled);
                connect(togglebox, &QCheckBox::toggled, this, [lineedit, option](bool checked) {
                    lineedit->setEnabled(checked);
                    option->enabled = checked;
                });
                optionlayout->addWidget(togglebox);
            }

            layout->addWidget(optionwidget, row, 1, Qt::AlignTop);
            layout->setRowMinimumHeight(layout->rowCount() - 1, rowheight);
        }
        else if (option->type.toLower() == "intslider") {
            QWidget* optionwidget = new QWidget(widget.data());
            QVBoxLayout* optionlayout = new QVBoxLayout(optionwidget);
            optionlayout->setContentsMargins(margins);

            QHBoxLayout* sliderlayout = new QHBoxLayout();
            QSlider* slider = new QSlider(Qt::Horizontal, widget.data());

            int minvalue = option->minimum.toInt();
            int maxvalue = option->maximum.toInt();
            int value = option->value.toInt();

            slider->setRange(minvalue, maxvalue);
            slider->setValue(value);

            QLineEdit* lineedit = intedit(option, widget.data());
            lineedit->setFixedWidth(60);
            sliderlayout->addWidget(slider);
            sliderlayout->addWidget(lineedit);

            connect(slider, &QSlider::valueChanged, this, [=](int newValue) {
                lineedit->setText(QString::number(newValue));
                valueChanged(option->id, newValue);
            });

            connect(lineedit, &QLineEdit::editingFinished, this, [=]() {
                bool valid;
                int typedValue = lineedit->text().toInt(&valid);
                if (valid) {
                    typedValue = std::clamp(typedValue, minvalue, maxvalue);
                    slider->setValue(typedValue);
                }
            });

            optionlayout->addLayout(sliderlayout);

            if (!option->toggle.isEmpty()) {
                QCheckBox* togglebox = new QCheckBox(option->toggle, optionwidget);
                togglebox->setFont(font);
                togglebox->setChecked(option->enabled);
                slider->setEnabled(option->enabled);
                lineedit->setEnabled(option->enabled);
                connect(togglebox, &QCheckBox::toggled, this, [=](bool checked) {
                    slider->setEnabled(checked);
                    lineedit->setEnabled(checked);
                    option->enabled = checked;
                });
                optionlayout->addWidget(togglebox);
            }

            layout->addWidget(optionwidget, row, 1, Qt::AlignTop);
            layout->setRowMinimumHeight(layout->rowCount() - 1, rowheight);
        }
        else if (option->type.toLower() == "label") {
            QWidget* optionwidget = new QWidget(widget.data());
            QVBoxLayout* optionlayout = new QVBoxLayout(optionwidget);
            optionlayout->setContentsMargins(margins);

            QLabel* label = new QLabel(option->value.toString(), widget.data());
            label->setFont(font);

            optionlayout->addWidget(label);

            if (!option->toggle.isEmpty()) {
                QCheckBox* togglebox = new QCheckBox(option->toggle, optionwidget);
                togglebox->setFont(font);
                togglebox->setChecked(option->enabled);
                label->setEnabled(option->enabled);
                connect(togglebox, &QCheckBox::toggled, this, [label, option](bool checked) {
                    label->setEnabled(checked);
                    option->enabled = checked;
                });
                optionlayout->addWidget(togglebox);
            }
            layout->addWidget(optionwidget, row, 1, Qt::AlignTop);
            layout->setRowMinimumHeight(layout->rowCount() - 1, rowheight);
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
                togglebox->setFont(font);
                togglebox->setChecked(option->enabled);
                lineedit->setEnabled(option->enabled);
                connect(togglebox, &QCheckBox::toggled, this, [lineedit, option](bool checked) {
                    lineedit->setEnabled(checked);
                    option->enabled = checked;
                });
                optionlayout->addWidget(togglebox);
            }
            layout->addWidget(optionwidget, row, 1, Qt::AlignTop);
            layout->setRowMinimumHeight(layout->rowCount() - 1, rowheight);
        }
        row++;
    }
    layout->setColumnStretch(0, 1);
    layout->setColumnStretch(1, 3);
    layout->addItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding), row, 0, 1, 2);
    widget->setLayout(layout);
}

QLineEdit*
OptionsWidgetPrivate::doubleedit(const QSharedPointer<Option>& option, QWidget* parent)
{
    QString value = utils::formatDouble(option->value.toDouble());
    QLineEdit* lineedit = new QLineEdit(value, parent);
    QDoubleValidator* validator = new QDoubleValidator(option->minimum.toDouble(), option->maximum.toDouble(),
                                                       editprecision, lineedit);
    validator->setNotation(QDoubleValidator::StandardNotation);
    validator->setLocale(QLocale::C);  // use '.' instead of ','
    lineedit->setValidator(validator);
    lineedit->setFont(font);
    lineedit->setCursorPosition(0);

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
                    lineedit->setText(QString::number(value));
                }
                else if (value > maximum) {
                    value = maximum;
                    lineedit->setText(QString::number(value));
                }
                valueChanged(option->id, value);
            }
        }
    });

    connect(lineedit, &QLineEdit::editingFinished, this, [lineedit]() {
        QString text = lineedit->text();
        bool valid;
        double value = text.toDouble(&valid);
        if (valid) {
            lineedit->setText(utils::formatDouble(value));
        }
    });
    return lineedit;
}

QLineEdit*
OptionsWidgetPrivate::intedit(const QSharedPointer<Option>& option, QWidget* parent)
{
    int value = static_cast<int>(option->value.toDouble());  // needed toInt() does not handle decimals
    QLineEdit* lineedit = new QLineEdit(QString::number(value), parent);
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
    return lineedit;
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
