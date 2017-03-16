/*
 * Chrome Token Signing Native Host
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "qt_certselect.h"

#include "Common.h"
#include "util.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

std::vector<unsigned char> QtCertSelect::getCert(const std::vector<std::vector<unsigned char>> &certs, const QString &origin, CertificatePurpose type)
{
    QtCertSelect dialog(origin, type);
    QTreeWidget *table = dialog.findChild<QTreeWidget*>();
    // Construct the list that is shown to the user.
    for (const std::vector<unsigned char> &c: certs)
    {
        QSslCertificate cert = v2cert(c);
        // filter out expired certificates and add to the funky list
        if (QDateTime::currentDateTime() >= cert.expiryDate())
            continue;
        table->insertTopLevelItem(0, new QTreeWidgetItem(table, QStringList{
            cert.subjectInfo(QSslCertificate::CommonName).at(0),
            cert.subjectInfo(QSslCertificate::Organization).at(0),
            cert.expiryDate().toString("dd.MM.yyyy"),
            QString::number(&c - &certs[0])})); // Index of certs list
    }

    table->setCurrentIndex(table->model()->index(0, 0));
    if (dialog.exec() == 0)
        throw UserCanceledError();
    return certs[table->currentItem()->text(3).toUInt()];
}


QtCertSelect::QtCertSelect(const QString &origin, CertificatePurpose type)
{
    setWindowFlags(Qt::WindowStaysOnTopHint);
    // remove minimize and fullscreen buttons (maximize disables sizegrip on osx)
    setWindowFlags((windowFlags()|Qt::CustomizeWindowHint) &
        ~(/*Qt::WindowMaximizeButtonHint|*/Qt::WindowFullscreenButtonHint|Qt::WindowMinimizeButtonHint|Qt::WindowCloseButtonHint));
    setWindowTitle(tr("Select certificate for %1 on %2").arg(type == Signing?tr("signing"):tr("authentication")).arg(origin));
    setMinimumWidth(500);

    QLabel *message = new QLabel(tr("Selected certificate will be forwarded to the remote website"), this);

    QTreeWidget *table = new QTreeWidget(this);
    table->setColumnCount(3);
    table->setRootIsDecorated(false);
    table->setHeaderLabels({tr("Certificate"), tr("Type"), tr("Valid to")});
    table->header()->setStretchLastSection(false);
    table->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->header()->setSectionResizeMode(0, QHeaderView::Stretch);

    QDialogButtonBox *buttons = new QDialogButtonBox(this);
    QPushButton *ok = buttons->addButton(tr("Select"), QDialogButtonBox::AcceptRole);
    buttons->addButton(tr("Cancel"), QDialogButtonBox::RejectRole);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(table, &QTreeWidget::clicked, [=]{
        ok->setEnabled(true);
    });

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(message);
    layout->addWidget(table);
    layout->addWidget(buttons);
}
