/**********************************************************************
 *  mainwindow.cpp
 **********************************************************************
 * Copyright (C) 2019 MX Authors
 *
 * Authors: Dolphin Oracle
 *          MX Linux <http://mxlinux.org>
 *          using live-usb-maker by BitJam
 *          and mx-live-usb-maker gui by adrian
 *
 * This is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this package. If not, see <http://www.gnu.org/licenses/>.
 **********************************************************************/


#include "about.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "version.h"

#include <QFileDialog>
#include <QScrollBar>
#include <QTextStream>

#include <QDebug>

MainWindow::MainWindow(const QStringList& args)  :
    ui(new Ui::MainWindow)
{
    qDebug().noquote() << QCoreApplication::applicationName() << "version:" << VERSION;
    ui->setupUi(this);
    setWindowFlags(Qt::Window); // for the close, min and max buttons
    setup();
    ui->combo_Usb->addItems(buildUsbList());
    this->adjustSize();
}

MainWindow::~MainWindow()
{
    delete ui;
}


void MainWindow::makeUsb(const QString &options)
{
    //get a device value if action is on a partition
    QString device_to_check = device;
    if (device.contains("nvme")){
        device_to_check = device.section("p",0,0);
    }
    if (device.contains("mmc")){
        device_to_check = device.section("p",0,0);
    }

    if (device.contains("sd")){
        device_to_check = device.left(3);
    }

    // check amount of io on device before copy, this is in sectors
    start_io = cmd->getCmdOut("cat /sys/block/" + device_to_check + "/stat |awk '{print $7}'").toInt();
    ui->progressBar->setMinimum(start_io);
    qDebug() << "start io is " << start_io;
    ui->progressBar->setMaximum(iso_sectors+start_io);
    qDebug() << "max progress bar is " << ui->progressBar->maximum();
    //clear partitions
    //qDebug() << cmd->getCmdOut("live-usb-maker gui partition-clear --color=off -t " + device);
    QString cmdstr = options;
    //QString cmdstr = "echo test options";
    setConnections();
    qDebug() << cmd->getCmdOut(cmdstr);
    //label drive
    //labeldrive();
}

// setup versious items first time program runs
void MainWindow::setup()
{
    cmd = new Cmd(this);
    cmdprog = new Cmd(this);
    connect(qApp, &QApplication::aboutToQuit, this, &MainWindow::cleanup);
    this->setWindowTitle("Format USB");
    advancedOptions = false;
    ui->buttonBack->setHidden(true);;
    ui->stackedWidget->setCurrentIndex(0);
    ui->buttonCancel->setEnabled(true);
    ui->buttonNext->setEnabled(true);
    ui->outputBox->setCursorWidth(0);
    height = this->heightMM();
    ui->lineEditFSlabel->setText("USB-DATA");

}

// Build the option list to be passed to live-usb-maker
QString MainWindow::buildOptionList()
{
    device = ui->combo_Usb->currentText().split(" ").at(0);
    label = ui->lineEditFSlabel->text();
    QString format = ui->comboBoxDataFormat->currentText();
    if (format.contains("fat32")){
        format = "vfat";
    }
    QString options;
    if (ui->checkBoxshowpartitions->isChecked()) {
        qDebug() << "partition is" << device << "label " << label;
        options = QString("su-to-root -X -c '/usr/lib/formatusb/formatusb_lib \"" + device + "\" " + format + " \"" + label + "\" part'");

    } else {
        qDebug() << "usb device" << device << "label " << label;
        options = QString("su-to-root -X -c '/usr/lib/formatusb/formatusb_lib \"" + device + "\" " + format + " \"" + label + "\"'");
    }
    qDebug() << "Options: " << options;
    return options;
}

// cleanup environment when window is closed
void MainWindow::cleanup()
{
    QString log_name = "/tmp/formatusb.log";
    system("[ -f " + log_name.toUtf8() + " ] && rm " + log_name.toUtf8());
}

// build the USB list
QStringList MainWindow::buildUsbList()
{   QString drives;
    if (ui->checkBoxshowpartitions->isChecked()){
        drives = cmd->getCmdOut("lsblk -nlo NAME,SIZE,LABEL,TYPE -I 3,8,22,179,259 |grep -v disk");
    } else {
        drives = cmd->getCmdOut("lsblk --nodeps -nlo NAME,SIZE,MODEL,VENDOR -I 3,8,22,179,259 ");
    }

    return removeUnsuitable(drives.split("\n"));
}

// remove unsuitable drives from the list (live and unremovable)
QStringList MainWindow::removeUnsuitable(const QStringList &devices)
{
    QStringList list;
    QString name;
    bool showall = ui->checkBoxShowAll->isChecked();
    for (const QString &line : devices) {
        name = line.split(" ").at(0);
        if (!showall){
            if (system(cli_utils.toUtf8() + "is_usb_or_removable " + name.toUtf8()) == 0) {
                if (cmd->getCmdOut(cli_utils + "get_drive $(get_live_dev) ") != name) {
                    list << line;
                }
            }
        } else {
            if (cmd->getCmdOut(cli_utils + "get_drive $(get_live_dev) ") != name) {
                list << line;
            }
        }
    }

    return list;
}

void MainWindow::cmdStart()
{
    //setCursor(QCursor(Qt::BusyCursor));
    //ui->lineEdit->setFocus();
}


void MainWindow::cmdDone()
{
    ui->progressBar->setValue(ui->progressBar->maximum());
    setCursor(QCursor(Qt::ArrowCursor));
    ui->buttonBack->setEnabled(true);
    if (cmd->exitCode() == 0 && cmd->exitStatus() == QProcess::NormalExit) {
        QMessageBox::information(this, tr("Success"), tr("Format successful!"));
    } else {
        QMessageBox::critical(this, tr("Failure"), tr("Error encountered in the Format process"));
    }
    cmd->disconnect();
    timer.stop();
}

// set proc and timer connections
void MainWindow::setConnections()
{
    timer.start(1000);
    connect(cmd, &QProcess::readyRead, this, &MainWindow::updateOutput);
    connect(cmd, &QProcess::started, this, &MainWindow::cmdStart);
    connect(&timer, &QTimer::timeout, this, &MainWindow::updateBar);
    connect(cmd, static_cast<void (QProcess::*)(int)>(&QProcess::finished), this, &MainWindow::cmdDone);

}

void MainWindow::updateBar()
{
    if (!ui->checkBoxshowpartitions->isChecked()){
        int current_io = cmdprog->getCmdOut("cat /sys/block/" + device + "/stat | awk '{print $7}'").toInt();
        ui->progressBar->setValue(current_io);
    }
}

void MainWindow::updateOutput()
{
    // remove escape sequences that are not handled by code
    QString out = cmd->readAll();
    out.remove("[0m").remove("]0;").remove("").remove("").remove("[1000D").remove("[74C|").remove("[?25l").remove("[?25h").remove("[0;36m").remove("[1;37m");
//    if (out.contains("[10D[K")) { // escape sequence used to display the progress percentage
//        out.remove("[10D[K");
//        ui->outputBox->moveCursor(QTextCursor::StartOfLine);
//        QKeyEvent *event = new QKeyEvent(QEvent::KeyPress, Qt::Key_K, Qt::ControlModifier);
//        QCoreApplication::postEvent(ui->outputBox, event);
//        QString out_prog = out;
//        ui->progressBar->setValue(out_prog.remove(" ").remove("%").toInt());
//    }

    ui->outputBox->insertPlainText(out);

    QScrollBar *sb = ui->outputBox->verticalScrollBar();
    sb->setValue(sb->maximum());
    qApp->processEvents();
}

// Next button clicked
void MainWindow::on_buttonNext_clicked()
{

    // on first page
    if (ui->stackedWidget->currentIndex() == 0) {
        if (ui->combo_Usb->currentText() == "") {
            QMessageBox::critical(this, tr("Error"), tr("Please select a USB device to write to"));
            return;
        }

        //confirm action
        int ans;
        QString msg = tr("These actions will destroy all data on \n\n") + ui->combo_Usb->currentText().simplified() + "\n\n " + tr("Do you wish to continue?");
        ans = QMessageBox::warning(this, windowTitle(), msg,
                                   QMessageBox::Yes, QMessageBox::No);
        if (ans != QMessageBox::Yes) {
            return;
        }
        if (cmd->state() != QProcess::NotRunning) {
            ui->stackedWidget->setCurrentWidget(ui->outputPage);
            return;
        }
        ui->buttonBack->setHidden(false);
        ui->buttonBack->setEnabled(false);
        ui->buttonNext->setEnabled(false);
        ui->stackedWidget->setCurrentWidget(ui->outputPage);

        makeUsb(buildOptionList());

    // on output page
    } else if (ui->stackedWidget->currentWidget() == ui->outputPage) {

    } else {
        return qApp->quit();
    }
}

void MainWindow::on_buttonBack_clicked()
{
    this->setWindowTitle("Format USB Device");
    ui->stackedWidget->setCurrentIndex(0);
    ui->buttonNext->setEnabled(true);
    ui->buttonBack->setDisabled(true);
    ui->outputBox->clear();
    ui->progressBar->setValue(0);
}


// About button clicked
void MainWindow::on_buttonAbout_clicked()
{
    this->hide();
    displayAboutMsgBox(tr("About %1").arg(this->windowTitle()), "<p align=\"center\"><b><h2>" + this->windowTitle() +"</h2></b></p><p align=\"center\">" +
                       tr("Version: ") + VERSION + "</p><p align=\"center\"><h3>" +
                       tr("Program for formatting USB devices") +
                       "</h3></p><p align=\"center\"><a href=\"http://mxlinux.org\">http://mxlinux.org</a><br /></p><p align=\"center\">" +
                       tr("Copyright (c) MX Linux") + "<br /><br /></p>",
                       "/usr/share/doc/formatusb/license.html", tr("%1 License").arg(this->windowTitle()), true);
    this->show();
}

// Help button clicked
void MainWindow::on_buttonHelp_clicked()
{
    QString url = "file:///usr/share/doc/formatusb/help/formatusb.html";
    displayDoc(url, tr("%1 Help").arg(this->windowTitle()), true);
}

void MainWindow::on_buttonRefresh_clicked()
{
    ui->combo_Usb->clear();
    ui->combo_Usb->addItems(buildUsbList());
}


void MainWindow::on_checkBoxShowAll_clicked()
{
    on_buttonRefresh_clicked();
}

void MainWindow::on_checkBoxshowpartitions_clicked()
{
     on_buttonRefresh_clicked();
}
