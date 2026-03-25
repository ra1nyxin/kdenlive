#include "listdependencyparamwidget.h"
#include "assets/model/assetparametermodel.hpp"
#include "core.h"
#include "mainwindow.h"

#include <QDir>
#include <QDomDocument>
#include <QDomElement>
#include <QStandardPaths>
#include <QDebug>

#include <KIO/JobUiDelegateFactory>
#include <KIO/OpenUrlJob>

ListDependencyParamWidget::ListDependencyParamWidget(std::shared_ptr<AssetParameterModel> model, QModelIndex index, QWidget *parent)
    : AbstractParamWidget(std::move(model), index, parent)
{
    setupUi(this);
    m_infoMessage->hide();
    connect(m_infoMessage, &KMessageWidget::linkActivated, this, [this](const QString &contents) {
        const QUrl linkUrl(contents);
        if (linkUrl.isLocalFile()) {
            const QString targetPath = linkUrl.toLocalFile();
            const QString appDataRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
            if (QDir(appDataRoot).contains(targetPath) || targetPath.startsWith(appDataRoot)) {
                QFileInfo info(targetPath);
                if (!info.exists()) {
                    QDir parentDir = info.absoluteDir();
                    if (!parentDir.exists()) {
                        parentDir.mkpath(QStringLiteral("."));
                    }
                }
            } else {
                qWarning() << "[Security] Blocked directory creation attempt outside sandbox:" << targetPath;
            }
        }
        auto *job = new KIO::OpenUrlJob(linkUrl);
        job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, this));
        job->start();
    });
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    m_list->setIconSize(QSize(50, 30));
    setMinimumHeight(m_list->sizeHint().height());
    const QString dependencies = m_model->data(m_index, AssetParameterModel::ListDependenciesRole).toString();
    if (!dependencies.isEmpty()) {
        QDomDocument doc;
        if (doc.setContent(dependencies)) {
            const QDomNodeList deps = doc.elementsByTagName(QLatin1String("paramdependencies"));
            const int depCount = deps.count();
            for (int i = 0; i < depCount; ++i) {
                const QDomElement el = deps.at(i).toElement();
                const QString modelName = el.attribute(QLatin1String("value"));
                QString infoText = el.text();
                const QString folder = el.attribute(QLatin1String("folder"));

                if (!folder.isEmpty()) {
                    m_dependencyFiles.insert(modelName, {folder, el.attribute(QLatin1String("files")).split(QLatin1Char(';'), Qt::SkipEmptyParts)});
                    const QString fullPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + folder;
                    infoText.replace(QLatin1String("%folder"), QDir::toNativeSeparators(fullPath));
                }
                m_dependencyInfos.insert(modelName, infoText);
            }
        }
    }
    slotRefresh();
    connect(m_list, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index < 0) return;
        const QString val = m_list->itemData(index).toString();
        Q_EMIT valueChanged(m_index, val, true);
        checkDependencies(val);
    });
}

void ListDependencyParamWidget::setCurrentIndex(int index)
{
    if (index >= 0 && index < m_list->count()) {
        m_list->setCurrentIndex(index);
    }
}

void ListDependencyParamWidget::setCurrentText(const QString &text)
{
    m_list->setCurrentText(text);
}

void ListDependencyParamWidget::addItem(const QString &text, const QVariant &value)
{
    m_list->addItem(text, value);
}

void ListDependencyParamWidget::setItemIcon(int index, const QIcon &icon)
{
    m_list->setItemIcon(index, icon);
}

void ListDependencyParamWidget::setIconSize(const QSize &size)
{
    m_list->setIconSize(size);
}

void ListDependencyParamWidget::slotShowComment(bool /*show*/) {}

QString ListDependencyParamWidget::getValue()
{
    return m_list->currentData().toString();
}

void ListDependencyParamWidget::checkDependencies(const QString &val)
{
    bool missingDep = false;
    if (m_dependencyInfos.contains(val)) {
        if (m_dependencyFiles.contains(val)) {
            const auto &fileData = m_dependencyFiles.value(val);
            const QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + fileData.first;
            const QDir dir(baseDir);
            if (fileData.first == QLatin1String("/opencvmodels")) {
                m_model->setParameter(QStringLiteral("modelsfolder"), dir.absolutePath(), false);
            }
            for (const QString &file : std::as_const(fileData.second)) {
                if (!dir.exists(file)) {
                    m_infoMessage->setText(m_dependencyInfos.value(val));
                    m_infoMessage->animatedShow();
                    setMinimumHeight(m_list->sizeHint().height() + m_infoMessage->sizeHint().height());
                    Q_EMIT updateHeight();
                    missingDep = true;
                    break;
                }
            }
        }
    }
    if (!missingDep) {
        m_infoMessage->hide();
        setMinimumHeight(m_list->sizeHint().height());
        Q_EMIT updateHeight();
    }
}

void ListDependencyParamWidget::slotRefresh()
{
    const QSignalBlocker bk(m_list);
    m_list->clear();
    const QStringList names = m_model->data(m_index, AssetParameterModel::ListNamesRole).toStringList();
    const QStringList values = m_model->data(m_index, AssetParameterModel::ListValuesRole).toStringList();
    const QString currentValue = m_model->data(m_index, AssetParameterModel::ValueRole).toString();
    if (currentValue != m_lastProcessedAlgo) {
        checkDependencies(currentValue);
        m_lastProcessedAlgo = currentValue;
    }
    if (!values.isEmpty() && values.first() == QLatin1String("%lumaPaths")) {
        const QStringList lumaPaths = pCore->getLumasForProfile();
        m_list->addItem(i18n("None (Dissolve)"), QString());
        for (int j = 0; j < lumaPaths.count(); ++j) {
            const QString &path = lumaPaths.at(j);
            const QString fileName = path.section(QLatin1Char('/'), -1);
            m_list->addItem(pCore->nameForLumaFile(fileName), path);            
            if (path.endsWith(QLatin1String(".png")) || path.endsWith(QLatin1String(".pgm"))) {
                if (MainWindow::m_lumacache.contains(path)) {
                    m_list->setItemIcon(m_list->count() - 1, QPixmap::fromImage(MainWindow::m_lumacache.value(path)));
                }
            }
        }
        if (!currentValue.isEmpty()) {
            int idx = m_list->findData(currentValue);
            if (idx != -1) m_list->setCurrentIndex(idx);
        }
    } else {
        const int count = qMin(names.count(), values.count());
        if (count > 0) {
            for (int i = 0; i < count; ++i) {
                m_list->addItem(names.at(i), values.at(i));
            }
        } else if (!values.isEmpty()) {
            for (const QString &v : values) m_list->addItem(v, v);
        }
        if (!currentValue.isEmpty()) {
            int ix = m_list->findData(currentValue);
            if (ix > -1) m_list->setCurrentIndex(ix);
        }
    }
}
