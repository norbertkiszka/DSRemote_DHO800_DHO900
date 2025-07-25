/*
***************************************************************************
*
* Author: Teunis van Beelen
*
* Copyright (C) 2015 - 2023 Teunis van Beelen
*
* Email: teuniz@protonmail.com
*
***************************************************************************
*
* This program is free software: you can redistribute it and/or modify
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
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
***************************************************************************
*/


#include "mainwindow.h"
#include "mainwindow_constr.cpp"
#include "timer_handlers.cpp"
#include "save_data.cpp"
#include "interface.cpp"
#include "serial_decoder.cpp"



void UI_Mainwindow::open_settings_dialog()
{
  UI_settings_window settings(this);
}


void UI_Mainwindow::open_connection()
{
  int i, j, n, len;

  char str[4096] = {""},
       dev_str[256] = {""},
       resp_str[2048] = {""},
       *ptr;

  QSettings settings;

  QMessageBox msgBox;

  lan_connect_thread lan_cn_thrd;

  if(device != NULL)
  {
    return;
  }

  if(devparms.connected)
  {
    return;
  }

  strlcpy(str, settings.value("connection/type", "USB").toString().toLatin1().data(), 4096);

  if(!strcmp(str, "LAN"))
  {
    devparms.connectiontype = 1;
  }
  else
  {
    devparms.connectiontype = 0;
  }

  if(devparms.connectiontype == 0)  // USB
  {
    strlcpy(dev_str, settings.value("connection/device", "/dev/usbtmc0").toString().toLatin1().data(), 256);

    if(!strcmp(dev_str, ""))
    {
      strlcpy(dev_str, "/dev/usbtmc0", 256);

      settings.setValue("connection/device", QString(dev_str));
    }

    device = tmc_open_usb(dev_str);
    if(device == NULL)
    {
      snprintf(str, 4096, "Can not open device %s", dev_str);
      goto OC_OUT_ERROR;
    }
  }

  if(devparms.connectiontype == 1)  // LAN
  {
    strlcpy(devparms.hostname, settings.value("connection/hostname", "").toString().toLatin1().data(), 128);

    if(strlen(devparms.hostname))
    {
      strlcpy(dev_str, devparms.hostname, 256);
    }
    else
    {
      strlcpy(dev_str, settings.value("connection/ip", "192.168.1.100").toString().toLatin1().data(), 256);

      if(!strcmp(dev_str, ""))
      {
        snprintf(str, 4096, "No IP address or hostname set");
        goto OC_OUT_ERROR;
      }

      len = strlen(dev_str);

      if(len < 7)
      {
        snprintf(str, 4096, "No IP address set");
        goto OC_OUT_ERROR;
      }

      int cf = 0;

      for(i=0; i<len; i++)
      {
        if(dev_str[i] == '.')
        {
          cf = 0;
        }

        if(dev_str[i] == '0')
        {
          if(cf == 0)
          {
            if((dev_str[i+1] != 0) && (dev_str[i+1] != '.'))
            {
              for(j=i; j<len; j++)
              {
                dev_str[j] = dev_str[j+1];
              }

              i--;

              len--;
            }
          }
        }
        else
        {
          if(dev_str[i] != '.')
          {
            cf = 1;
          }
        }
      }
    }

    statusLabel->setText("Trying to connect...");

    snprintf(str, 4096, "Trying to connect to %s", dev_str);

    msgBox.setIcon(QMessageBox::NoIcon);
    msgBox.setText(str);
    msgBox.addButton("Abort", QMessageBox::RejectRole);

    lan_cn_thrd.set_device_address(dev_str);
    connect(&lan_cn_thrd, SIGNAL(finished()), &msgBox, SLOT(accept()));
    lan_cn_thrd.start();

    if(msgBox.exec() != QDialog::Accepted)
    {
      statusLabel->setText("Connection aborted");
      lan_cn_thrd.terminate();
      lan_cn_thrd.wait(20000);
      snprintf(str, 4096, "Connection aborted");
      disconnect(&lan_cn_thrd, 0, 0, 0);
      goto OC_OUT_ERROR;
    }

    disconnect(&lan_cn_thrd, 0, 0, 0);

    device = lan_cn_thrd.get_device();
    if(device == NULL)
    {
      statusLabel->setText("Connection failed");
      snprintf(str, 4096, "Can not open connection to %s", dev_str);
      goto OC_OUT_ERROR;
    }
  }

  if(tmc_write("*IDN?") != 5)
//  if(tmc_write("*IDN?;:SYST:ERR?") != 16)  // This is a fix for the broken *IDN? command in older fw version
  {
    snprintf(str, 4096, "Can not write to device %s", dev_str);
    goto OC_OUT_ERROR;
  }

  n = tmc_read();

  if(n < 0)
  {
    snprintf(str, 4096, "Can not read from device %s", dev_str);
    goto OC_OUT_ERROR;
  }

  devparms.channel_cnt = 0;

  devparms.bandwidth = 0;

  devparms.modelname[0] = 0;

  strlcpy(resp_str, device->buf, 1024);

  ptr = strtok(resp_str, ",");
  if(ptr == NULL)
  {
    snprintf(str, 1024, "Received an unknown identification string from device:\n\n%s\n ", device->buf);
    goto OC_OUT_ERROR;
  }

  if(strcmp(ptr, "RIGOL TECHNOLOGIES") && strcmp(ptr, "NK TECHNOLOGIES"))
  {
    snprintf(str, 1024, "Received an unknown identification string from device:\n\n%s\n ", device->buf);
    goto OC_OUT_ERROR;
  }

  ptr = strtok(NULL, ",");
  if(ptr == NULL)
  {
    snprintf(str, 1024, "Received an unknown identification string from device:\n\n%s\n ", device->buf);
    goto OC_OUT_ERROR;
  }

  get_device_model(ptr);

  if((!devparms.channel_cnt) || (!devparms.bandwidth))
  {
    snprintf(str, 1024, "Received an unknown identification string from device:\n\n%s\n ", device->buf);
    goto OC_OUT_ERROR;
  }

  ptr = strtok(NULL, ",");
  if(ptr == NULL)
  {
    snprintf(str, 1024, "Received an unknown identification string from device:\n\n%s\n ", device->buf);
    goto OC_OUT_ERROR;
  }

  strlcpy(devparms.serialnr, ptr, 128);

  ptr = strtok(NULL, ",");
  if(ptr == NULL)
  {
    snprintf(str, 1024, "Received an unknown identification string from device:\n\n%s\n ", device->buf);
    goto OC_OUT_ERROR;
  }

  strlcpy(devparms.softwvers, ptr, 128);

  for(i=0; ; i++)
  {
    if(devparms.softwvers[i] == 0)  break;

    if(devparms.softwvers[i] == ';')
    {
      devparms.softwvers[i] = 0;

      break;
    }
  }

  if((devparms.modelserie != 6) &&
     (devparms.modelserie != 1))
  {
    if(devparms.modelserie == 7)
    {
      msgBox.setIcon(QMessageBox::Warning);
      msgBox.setText("DHO800/900 device detected.");
      msgBox.setInformativeText("DHO800 and DHO900 series support is at experimental stage.\n\n"
        "If you continue, it's likely that the program will not work correctly at some points.\n"
        "\nDo you want to continue?\n");
    }
    else
    {
      msgBox.setIcon(QMessageBox::Warning);
      msgBox.setText("Unsupported device detected.");
      msgBox.setInformativeText("This software has not been tested with your device.\n"
        "It has been tested with the DS6000 and DS1000Z series only.\n"
        "If you continue, it's likely that the program will not work correctly at some points.\n"
        "\nDo you want to continue?\n");
    }
    
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::Yes);
    int ret = msgBox.exec();

    if(ret != QMessageBox::Yes)
    {
      statusLabel->setText("Disconnected");

      devparms.connected = 0;

      close_connection();

      return;
    }
  }

  if(get_device_settings())
  {
    strlcpy(str, "Can not read device settings", 4096);

    goto OC_OUT_ERROR;
  }

  if(devparms.timebasedelayenable)
  {
    devparms.current_screen_sf = 100.0 / devparms.timebasedelayscale;
  }
  else
  {
    devparms.current_screen_sf = 100.0 / devparms.timebasescale;
  }

  if((devparms.modelserie == 1) || (devparms.modelserie == 2))
  {
    trig50pctButton->setEnabled(false);
  }

  if(devparms.channel_cnt < 4)
  {
    ch4Button->setEnabled(false);

    ch4Button->setVisible(false);
  }

  if(devparms.channel_cnt < 3)
  {
    ch3Button->setEnabled(false);

    ch3Button->setVisible(false);
  }

  if(devparms.channel_cnt < 2)
  {
    ch2Button->setEnabled(false);

    ch2Button->setVisible(false);
  }

  devparms.cmd_cue_idx_in = 0;
  devparms.cmd_cue_idx_out = 0;

  devparms.func_has_record = 0;

  devparms.fftbufsz = devparms.hordivisions * 50;

  if(devparms.k_cfg != NULL)
  {
    free(devparms.k_cfg);
  }
  devparms.k_cfg = kiss_fftr_alloc(devparms.fftbufsz * 2, 0, NULL, NULL);

  connect(adjDial,          SIGNAL(valueChanged(int)), this, SLOT(adjDialChanged(int)));
  connect(trigAdjustDial,   SIGNAL(valueChanged(int)), this, SLOT(trigAdjustDialChanged(int)));
  connect(horScaleDial,     SIGNAL(valueChanged(int)), this, SLOT(horScaleDialChanged(int)));
  connect(horPosDial,       SIGNAL(valueChanged(int)), this, SLOT(horPosDialChanged(int)));
  connect(vertOffsetDial,   SIGNAL(valueChanged(int)), this, SLOT(vertOffsetDialChanged(int)));
  connect(vertScaleDial,    SIGNAL(valueChanged(int)), this, SLOT(vertScaleDialChanged(int)));
  connect(navDial,          SIGNAL(valueChanged(int)), this, SLOT(navDialChanged(int)));

  connect(ch1Button,        SIGNAL(clicked()),      this, SLOT(ch1ButtonClicked()));
  connect(ch2Button,        SIGNAL(clicked()),      this, SLOT(ch2ButtonClicked()));
  connect(ch3Button,        SIGNAL(clicked()),      this, SLOT(ch3ButtonClicked()));
  connect(ch4Button,        SIGNAL(clicked()),      this, SLOT(ch4ButtonClicked()));
  connect(chanMenuButton,   SIGNAL(clicked()),      this, SLOT(chan_menu()));
  connect(mathMenuButton,   SIGNAL(clicked()),      this, SLOT(math_menu()));
  connect(waveForm,         SIGNAL(chan1Clicked()), this, SLOT(ch1ButtonClicked()));
  connect(waveForm,         SIGNAL(chan2Clicked()), this, SLOT(ch2ButtonClicked()));
  connect(waveForm,         SIGNAL(chan3Clicked()), this, SLOT(ch3ButtonClicked()));
  connect(waveForm,         SIGNAL(chan4Clicked()), this, SLOT(ch4ButtonClicked()));
  connect(clearButton,      SIGNAL(clicked()),      this, SLOT(clearButtonClicked()));
  connect(autoButton,       SIGNAL(clicked()),      this, SLOT(autoButtonClicked()));
  connect(runButton,        SIGNAL(clicked()),      this, SLOT(runButtonClicked()));
  connect(singleButton,     SIGNAL(clicked()),      this, SLOT(singleButtonClicked()));
  connect(horMenuButton,    SIGNAL(clicked()),      this, SLOT(horMenuButtonClicked()));
  connect(trigModeButton,   SIGNAL(clicked()),      this, SLOT(trigModeButtonClicked()));
  connect(trigMenuButton,   SIGNAL(clicked()),      this, SLOT(trigMenuButtonClicked()));
  connect(trigForceButton,  SIGNAL(clicked()),      this, SLOT(trigForceButtonClicked()));
  connect(trig50pctButton,  SIGNAL(clicked()),      this, SLOT(trig50pctButtonClicked()));
  connect(acqButton,        SIGNAL(clicked()),      this, SLOT(acqButtonClicked()));
  connect(cursButton,       SIGNAL(clicked()),      this, SLOT(cursButtonClicked()));
  connect(saveButton,       SIGNAL(clicked()),      this, SLOT(saveButtonClicked()));
  connect(dispButton,       SIGNAL(clicked()),      this, SLOT(dispButtonClicked()));
  connect(utilButton,       SIGNAL(clicked()),      this, SLOT(utilButtonClicked()));
  connect(helpButton,       SIGNAL(clicked()),      this, SLOT(helpButtonClicked()));
  connect(measureButton,    SIGNAL(clicked()),      this, SLOT(measureButtonClicked()));

  connect(select_chan1_act, SIGNAL(triggered()),    this, SLOT(ch1ButtonClicked()));
  connect(select_chan2_act, SIGNAL(triggered()),    this, SLOT(ch2ButtonClicked()));
  connect(select_chan3_act, SIGNAL(triggered()),    this, SLOT(ch3ButtonClicked()));
  connect(select_chan4_act, SIGNAL(triggered()),    this, SLOT(ch4ButtonClicked()));
  connect(toggle_fft_act,   SIGNAL(triggered()),    this, SLOT(toggle_fft()));

  connect(horPosDial,     SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(horPosDialClicked(QPoint)));
  connect(vertOffsetDial, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(vertOffsetDialClicked(QPoint)));
  connect(horScaleDial,   SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(horScaleDialClicked(QPoint)));
  connect(vertScaleDial,  SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(vertScaleDialClicked(QPoint)));
  connect(trigAdjustDial, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(trigAdjustDialClicked(QPoint)));
  connect(adjDial,        SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(adjustDialClicked(QPoint)));

  connect(playpauseButton, SIGNAL(clicked()), this, SLOT(playpauseButtonClicked()));
  connect(stopButton,      SIGNAL(clicked()), this, SLOT(stopButtonClicked()));
  connect(recordButton,    SIGNAL(clicked()), this, SLOT(recordButtonClicked()));

  snprintf(str, 4096, PROGRAM_NAME " " PROGRAM_VERSION "   %s   %s   %s",
          devparms.serialnr, devparms.softwvers, dev_str);
//   sprintf(str, PROGRAM_NAME " " PROGRAM_VERSION "   %s   %s",
//           devparms.softwvers, dev_str);

  setWindowTitle(str);

  statusLabel->setText("Connected");

  scrn_thread->set_device(device);

  devparms.connected = 1;

//  test_timer->start(2000);

  DPRwidget->setEnabled(true);

  devparms.screenupdates_on = 1;

  scrn_thread->h_busy = 0;

  scrn_timer->start(devparms.screentimerival);

  return;

OC_OUT_ERROR:

  statusLabel->setText("Disconnected");

  devparms.connected = 0;

  QMessageBox mesgbox;
  mesgbox.setIcon(QMessageBox::Critical);
  mesgbox.setText(str);
  mesgbox.exec();

  close_connection();
}


void UI_Mainwindow::close_connection()
{
  DPRwidget->setEnabled(false);

  test_timer->stop();

  scrn_timer->stop();

  adjdial_timer->stop();

  devparms.connected = 0;

  if(scrn_thread->wait(5000) == false)
  {
    scrn_thread->terminate();

    scrn_thread->wait(5000);

    pthread_mutex_trylock(&devparms.mutexx);
    pthread_mutex_unlock(&devparms.mutexx);

    scrn_thread->h_busy = 0;
  }

  devparms.screenupdates_on = 0;

  setWindowTitle(PROGRAM_NAME " " PROGRAM_VERSION);

  strlcpy(devparms.modelname, "-----", 128);

  adjDialFunc = ADJ_DIAL_FUNC_NONE;
  navDialFunc = NAV_DIAL_FUNC_NONE;

  disconnect(adjDial,         SIGNAL(valueChanged(int)), this, SLOT(adjDialChanged(int)));
  disconnect(trigAdjustDial,  SIGNAL(valueChanged(int)), this, SLOT(trigAdjustDialChanged(int)));
  disconnect(horScaleDial,    SIGNAL(valueChanged(int)), this, SLOT(horScaleDialChanged(int)));
  disconnect(horPosDial,      SIGNAL(valueChanged(int)), this, SLOT(horPosDialChanged(int)));
  disconnect(vertOffsetDial,  SIGNAL(valueChanged(int)), this, SLOT(vertOffsetDialChanged(int)));
  disconnect(vertScaleDial,   SIGNAL(valueChanged(int)), this, SLOT(vertScaleDialChanged(int)));
  disconnect(navDial,         SIGNAL(valueChanged(int)), this, SLOT(navDialChanged(int)));

  disconnect(ch1Button,        SIGNAL(clicked()),     this, SLOT(ch1ButtonClicked()));
  disconnect(ch2Button,        SIGNAL(clicked()),     this, SLOT(ch2ButtonClicked()));
  disconnect(ch3Button,        SIGNAL(clicked()),     this, SLOT(ch3ButtonClicked()));
  disconnect(ch4Button,        SIGNAL(clicked()),     this, SLOT(ch4ButtonClicked()));
  disconnect(chanMenuButton,   SIGNAL(clicked()),      this, SLOT(chan_menu()));
  disconnect(mathMenuButton,   SIGNAL(clicked()),      this, SLOT(math_menu()));
  disconnect(waveForm,         SIGNAL(chan1Clicked()), this, SLOT(ch1ButtonClicked()));
  disconnect(waveForm,         SIGNAL(chan2Clicked()), this, SLOT(ch2ButtonClicked()));
  disconnect(waveForm,         SIGNAL(chan3Clicked()), this, SLOT(ch3ButtonClicked()));
  disconnect(waveForm,         SIGNAL(chan4Clicked()), this, SLOT(ch4ButtonClicked()));
  disconnect(clearButton,      SIGNAL(clicked()),     this, SLOT(clearButtonClicked()));
  disconnect(autoButton,       SIGNAL(clicked()),     this, SLOT(autoButtonClicked()));
  disconnect(runButton,        SIGNAL(clicked()),     this, SLOT(runButtonClicked()));
  disconnect(singleButton,     SIGNAL(clicked()),     this, SLOT(singleButtonClicked()));
  disconnect(horMenuButton,    SIGNAL(clicked()),     this, SLOT(horMenuButtonClicked()));
  disconnect(trigModeButton,   SIGNAL(clicked()),     this, SLOT(trigModeButtonClicked()));
  disconnect(trigMenuButton,   SIGNAL(clicked()),     this, SLOT(trigMenuButtonClicked()));
  disconnect(trigForceButton,  SIGNAL(clicked()),     this, SLOT(trigForceButtonClicked()));
  disconnect(trig50pctButton,  SIGNAL(clicked()),     this, SLOT(trig50pctButtonClicked()));
  disconnect(acqButton,        SIGNAL(clicked()),     this, SLOT(acqButtonClicked()));
  disconnect(cursButton,       SIGNAL(clicked()),     this, SLOT(cursButtonClicked()));
  disconnect(saveButton,       SIGNAL(clicked()),     this, SLOT(saveButtonClicked()));
  disconnect(dispButton,       SIGNAL(clicked()),     this, SLOT(dispButtonClicked()));
  disconnect(utilButton,       SIGNAL(clicked()),     this, SLOT(utilButtonClicked()));
  disconnect(helpButton,       SIGNAL(clicked()),     this, SLOT(helpButtonClicked()));
  disconnect(measureButton,    SIGNAL(clicked()),     this, SLOT(measureButtonClicked()));

  disconnect(select_chan1_act, SIGNAL(triggered()),    this, SLOT(ch1ButtonClicked()));
  disconnect(select_chan2_act, SIGNAL(triggered()),    this, SLOT(ch2ButtonClicked()));
  disconnect(select_chan3_act, SIGNAL(triggered()),    this, SLOT(ch3ButtonClicked()));
  disconnect(select_chan4_act, SIGNAL(triggered()),    this, SLOT(ch4ButtonClicked()));
  disconnect(toggle_fft_act,   SIGNAL(triggered()),    this, SLOT(toggle_fft()));

  disconnect(horPosDial,     SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(horPosDialClicked(QPoint)));
  disconnect(vertOffsetDial, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(vertOffsetDialClicked(QPoint)));
  disconnect(horScaleDial,   SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(horScaleDialClicked(QPoint)));
  disconnect(vertScaleDial,  SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(vertScaleDialClicked(QPoint)));
  disconnect(trigAdjustDial, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(trigAdjustDialClicked(QPoint)));
  disconnect(adjDial,        SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(adjustDialClicked(QPoint)));

  disconnect(playpauseButton, SIGNAL(clicked()), this, SLOT(playpauseButtonClicked()));
  disconnect(stopButton,      SIGNAL(clicked()), this, SLOT(stopButtonClicked()));
  disconnect(recordButton,    SIGNAL(clicked()), this, SLOT(recordButtonClicked()));

  scrn_thread->set_device(NULL);

  devparms.math_fft = 0;

  devparms.math_fft_split = 0;

  waveForm->clear();

  tmc_close();

  device = NULL;

  if(devparms.k_cfg != NULL)
  {
    free(devparms.k_cfg);

    devparms.k_cfg = NULL;
  }

  statusLabel->setText("Disconnected");

  printf("Disconnected from device\n");
}


void UI_Mainwindow::closeEvent(QCloseEvent *cl_event)
{
  devparms.connected = 0;

  test_timer->stop();

  scrn_timer->stop();

  adjdial_timer->stop();

  scrn_thread->wait(5000);

  scrn_thread->terminate();

  scrn_thread->wait(5000);

  devparms.screenupdates_on = 0;

  scrn_thread->set_device(NULL);

  tmc_close();

  device = NULL;

  cl_event->accept();
}


int UI_Mainwindow::get_device_settings(int delay)
{
  int chn;

  char str[4096] = {""};

  statusLabel->setText("Reading instrument settings...");

  read_settings_thread rd_set_thrd;
  rd_set_thrd.set_device(device);
  rd_set_thrd.set_delay(delay);
  rd_set_thrd.set_devparm_ptr(&devparms);
  rd_set_thrd.start();

  QMessageBox msgBox;
  msgBox.setIcon(QMessageBox::NoIcon);
  msgBox.setText("Reading instrument settings...");
  msgBox.addButton("Abort", QMessageBox::RejectRole);

  connect(&rd_set_thrd, SIGNAL(finished()), &msgBox, SLOT(accept()));

  if(msgBox.exec() != QDialog::Accepted)
  {
    statusLabel->setText("Reading settings aborted");
    rd_set_thrd.terminate();
    rd_set_thrd.wait(20000);
    snprintf(str, 4096, "Reading settings aborted");
    disconnect(&rd_set_thrd, 0, 0, 0);
    return -1;
  }

  disconnect(&rd_set_thrd, 0, 0, 0);

  if(rd_set_thrd.get_error_num() != 0)
  {
    statusLabel->setText("Error while reading settings");
    rd_set_thrd.get_error_str(str, 4096);
    msgBox.setIcon(QMessageBox::Critical);
    msgBox.setText(str);
    msgBox.exec();
    strlcpy(str, "Can not read settings from device", 4096);
    return -1;
  }

  for(chn=0; chn<devparms.channel_cnt; chn++)
  {
    if(devparms.chandisplay[chn] == 1)
    {
      switch(chn)
      {
        case 0: ch1Button->setStyleSheet("background: #FFFF33;");
                break;
        case 1: ch2Button->setStyleSheet("background: #33FFFF;");
                break;
        case 2: ch3Button->setStyleSheet("background: #FF33FF;");
                break;
        case 3: ch4Button->setStyleSheet("background: #0080FF;");
                break;
      }
    }
    else
    {
      switch(chn)
      {
        case 0: ch1Button->setStyleSheet(def_stylesh);
                break;
        case 1: ch2Button->setStyleSheet(def_stylesh);
                break;
        case 2: ch3Button->setStyleSheet(def_stylesh);
                break;
        case 3: ch4Button->setStyleSheet(def_stylesh);
                break;
      }
    }
  }

  if(devparms.triggersweep == 0)
  {
    trigModeAutoLed->setValue(true);
    trigModeNormLed->setValue(false);
    trigModeSingLed->setValue(false);
  }
  else if(devparms.triggersweep == 1)
    {
      trigModeAutoLed->setValue(false);
      trigModeNormLed->setValue(true);
      trigModeSingLed->setValue(false);
    }
    else if(devparms.triggersweep == 2)
      {
        trigModeAutoLed->setValue(false);
        trigModeNormLed->setValue(false);
        trigModeSingLed->setValue(true);
      }

  updateLabels();

  return 0;
}


int UI_Mainwindow::parse_preamble(char *str, int sz, struct waveform_preamble *wfp, int chn)
{
  char *ptr;

  if(sz < 19)
  {
    return -1;
  }

  ptr = strtok(str, ",");
  if(ptr == NULL)
  {
    return -1;
  }

  wfp->format = atoi(ptr);

  ptr = strtok(NULL, ",");
  if(ptr == NULL)
  {
    return -1;
  }

  wfp->type = atoi(ptr);

  ptr = strtok(NULL, ",");
  if(ptr == NULL)
  {
    return -1;
  }

  wfp->points = atoi(ptr);

  ptr = strtok(NULL, ",");
  if(ptr == NULL)
  {
    return -1;
  }

  wfp->count = atoi(ptr);

  ptr = strtok(NULL, ",");
  if(ptr == NULL)
  {
    return -1;
  }

  wfp->xincrement[chn] = atof(ptr);

  ptr = strtok(NULL, ",");
  if(ptr == NULL)
  {
    return -1;
  }

  wfp->xorigin[chn] = atof(ptr);

  ptr = strtok(NULL, ",");
  if(ptr == NULL)
  {
    return -1;
  }

  wfp->xreference[chn] = atof(ptr);

  ptr = strtok(NULL, ",");
  if(ptr == NULL)
  {
    return -1;
  }

  wfp->yincrement[chn] = atof(ptr);

  ptr = strtok(NULL, ",");
  if(ptr == NULL)
  {
    return -1;
  }

  wfp->yorigin[chn] = atof(ptr);

  ptr = strtok(NULL, ",");
  if(ptr == NULL)
  {
    return -1;
  }

  wfp->yreference[chn] = atoi(ptr);

  ptr = strtok(NULL, ",");
  if(ptr != NULL)
  {
    return -1;
  }

  return 0;
}


int UI_Mainwindow::get_metric_factor(double value)
{
  int suffix=0;

  if(value < 0)
  {
      value *= -1;
  }

  if(value >= 1e12 && value < 1e15)
  {
      suffix = 12;
  }
  else if(value >= 1e9 && value < 1e12)
    {
        suffix = 9;
    }
    else if(value >= 1e6 && value < 1e9)
      {
          suffix = 6;
      }
      else if(value >= 1e3 && value < 1e6)
        {
          suffix = 3;
        }
        else if(value >= 1e-3 && value < 1)
          {
            suffix = -3;
          }
          else if( value >= 1e-6 && value < 1e-3)
            {
              suffix = -6;
            }
            else if(value >= 1e-9 && value < 1e-6)
              {
                suffix = -9;
              }
              else if(value >= 1e-12 && value < 1e-9)
                {
                  suffix = -12;
                }

  return suffix;
}


double UI_Mainwindow::get_stepsize_divide_by_1000(double val)
{
  int exp=0;

  if(val < 1e-9)
  {
    return 1e-9;
  }

  while(val < 1)
  {
    val *= 10;

    exp--;
  }

  while(val >= 10)
  {
    val /= 10;

    exp++;
  }

  val = exp10(exp - 2);

  if((val < 1e-13) && (val > -1e-13))
  {
    return 0;
  }

  return val;
}


void UI_Mainwindow::get_device_model(const char *str)
{
  devparms.channel_cnt = 0;

  devparms.bandwidth = 0;

  devparms.hordivisions = 14;

  devparms.vertdivisions = 8;

  if(!strcmp(str, "DS6104"))
  {
    devparms.channel_cnt = 4;

    devparms.bandwidth = 1000;

    devparms.modelserie = 6;
  }

  if(!strcmp(str, "DS6064"))
  {
    devparms.channel_cnt = 4;

    devparms.bandwidth = 600;

    devparms.modelserie = 6;
  }

  if(!strcmp(str, "DS6102"))
  {
    devparms.channel_cnt = 2;

    devparms.bandwidth = 1000;

    devparms.modelserie = 6;
  }

  if(!strcmp(str, "DS6062"))
  {
    devparms.channel_cnt = 2;

    devparms.bandwidth = 600;

    devparms.modelserie = 6;
  }

  if(!strcmp(str, "DS4012"))
  {
    devparms.channel_cnt = 2;

    devparms.bandwidth = 100;

    devparms.modelserie = 4;
  }

  if(!strcmp(str, "DS4014"))
  {
    devparms.channel_cnt = 4;

    devparms.bandwidth = 100;

    devparms.modelserie = 4;
  }

  if(!strcmp(str, "DS4022"))
  {
    devparms.channel_cnt = 2;

    devparms.bandwidth = 200;

    devparms.modelserie = 4;
  }

  if(!strcmp(str, "DS4024"))
  {
    devparms.channel_cnt = 4;

    devparms.bandwidth = 200;

    devparms.modelserie = 4;
  }

  if(!strcmp(str, "DS4032"))
  {
    devparms.channel_cnt = 2;

    devparms.bandwidth = 350;

    devparms.modelserie = 4;
  }

  if(!strcmp(str, "DS4034"))
  {
    devparms.channel_cnt = 4;

    devparms.bandwidth = 350;

    devparms.modelserie = 4;
  }

  if(!strcmp(str, "DS4052"))
  {
    devparms.channel_cnt = 2;

    devparms.bandwidth = 500;

    devparms.modelserie = 4;
  }

  if(!strcmp(str, "DS4054"))
  {
    devparms.channel_cnt = 4;

    devparms.bandwidth = 500;

    devparms.modelserie = 4;
  }

  if(!strcmp(str, "MSO4012"))
  {
    devparms.channel_cnt = 2;

    devparms.bandwidth = 100;

    devparms.modelserie = 4;
  }

  if(!strcmp(str, "MSO4024"))
  {
    devparms.channel_cnt = 4;

    devparms.bandwidth = 200;

    devparms.modelserie = 4;
  }

  if(!strcmp(str, "DS2072A"))
  {
    devparms.channel_cnt = 2;

    devparms.bandwidth = 70;

    devparms.modelserie = 2;
  }

  if(!strcmp(str, "DS2072A-S"))
  {
    devparms.channel_cnt = 2;

    devparms.bandwidth = 70;

    devparms.modelserie = 2;
  }

  if(!strcmp(str, "DS2102"))
  {
    devparms.channel_cnt = 2;

    devparms.bandwidth = 100;

    devparms.modelserie = 2;
  }

  if(!strcmp(str, "DS2102A"))
  {
    devparms.channel_cnt = 2;

    devparms.bandwidth = 100;

    devparms.modelserie = 2;
  }

  if(!strcmp(str, "DS2102A-S"))
  {
    devparms.channel_cnt = 2;

    devparms.bandwidth = 100;

    devparms.modelserie = 2;
  }

  if(!strcmp(str, "DS2202"))
  {
    devparms.channel_cnt = 2;

    devparms.bandwidth = 200;

    devparms.modelserie = 2;
  }

  if(!strcmp(str, "DS2202A"))
  {
    devparms.channel_cnt = 2;

    devparms.bandwidth = 200;

    devparms.modelserie = 2;
  }

  if(!strcmp(str, "DS2202A-S"))
  {
    devparms.channel_cnt = 2;

    devparms.bandwidth = 200;

    devparms.modelserie = 2;
  }

  if(!strcmp(str, "DS2302"))
  {
    devparms.channel_cnt = 2;

    devparms.bandwidth = 300;

    devparms.modelserie = 2;
  }

  if(!strcmp(str, "DS2302A"))
  {
    devparms.channel_cnt = 2;

    devparms.bandwidth = 300;

    devparms.modelserie = 2;
  }

  if(!strcmp(str, "DS2302A-S"))
  {
    devparms.channel_cnt = 2;

    devparms.bandwidth = 300;

    devparms.modelserie = 2;
  }

  if(!strcmp(str, "DS1054Z"))
  {
    devparms.channel_cnt = 4;

    devparms.bandwidth = 50;

    devparms.modelserie = 1;
  }

  if(!strcmp(str, "DS1074Z"))
  {
    devparms.channel_cnt = 4;

    devparms.bandwidth = 70;

    devparms.modelserie = 1;
  }

  if(!strcmp(str, "DS1074Z-S"))
  {
    devparms.channel_cnt = 4;

    devparms.bandwidth = 70;

    devparms.modelserie = 1;
  }

  if(!strcmp(str, "DS1074Z Plus"))
  {
    devparms.channel_cnt = 4;

    devparms.la_channel_cnt = 16;

    devparms.bandwidth = 70;

    devparms.modelserie = 1;
  }

  if(!strcmp(str, "DS1074Z-S Plus"))
  {
    devparms.channel_cnt = 4;

    devparms.la_channel_cnt = 16;

    devparms.bandwidth = 70;

    devparms.modelserie = 1;
  }

  if(!strcmp(str, "DS1104Z"))
  {
    devparms.channel_cnt = 4;

    devparms.bandwidth = 100;

    devparms.modelserie = 1;
  }

  if(!strcmp(str, "DS1104Z-S"))
  {
    devparms.channel_cnt = 4;

    devparms.bandwidth = 100;

    devparms.modelserie = 1;
  }

  if(!strcmp(str, "DS1104Z Plus"))
  {
    devparms.channel_cnt = 4;

    devparms.la_channel_cnt = 16;

    devparms.bandwidth = 100;

    devparms.modelserie = 1;
  }

  if(!strcmp(str, "DS1104Z-S Plus"))
  {
    devparms.channel_cnt = 4;

    devparms.la_channel_cnt = 16;

    devparms.bandwidth = 100;

    devparms.modelserie = 1;
  }

  if(!strcmp(str, "MSO1074Z"))
  {
    devparms.channel_cnt = 4;

    devparms.bandwidth = 70;

    devparms.modelserie = 1;
  }

  if(!strcmp(str, "MSO1104Z"))
  {
    devparms.channel_cnt = 4;

    devparms.bandwidth = 100;

    devparms.modelserie = 1;
  }

  if(!strcmp(str, "MSO1074Z-S"))
  {
    devparms.channel_cnt = 4;

    devparms.bandwidth = 70;

    devparms.modelserie = 1;
  }

  if(!strcmp(str, "MSO1104Z-S"))
  {
    devparms.channel_cnt = 4;

    devparms.bandwidth = 100;

    devparms.modelserie = 1;
  }

  if(!strcmp(str, "DS1202Z-E"))
  {
    devparms.channel_cnt = 2;

    devparms.bandwidth = 200;

    devparms.modelserie = 1;
  }

  if(!strcmp(str, "DS1102Z-E"))
  {
    devparms.channel_cnt = 2;

    devparms.bandwidth = 100;

    devparms.modelserie = 1;
  }
  
  if(!strcmp(str, "DHO802") || !strcmp(str, "DHO812"))
  {
    devparms.channel_cnt = 2;

    devparms.bandwidth = 625;

    devparms.modelserie = 7;

    devparms.hordivisions = 10;

    devparms.vertdivisions = 8;
  }
  
  if(!strcmp(str, "DHO804") || !strcmp(str, "DHO814") || !strcmp(str, "DHO914") || !strcmp(str, "DHO924") || !strcmp(str, "DHO914S") || !strcmp(str, "DHO924S"))
  {
    devparms.channel_cnt = 4;

    devparms.bandwidth = 625;

    devparms.modelserie = 7;

    devparms.hordivisions = 10;

    devparms.vertdivisions = 8;
  }

  if(devparms.modelserie == 1)
  {
    if(devparms.use_extra_vertdivisions)  devparms.vertdivisions = 10;

    devparms.hordivisions = 12;
  }

  if(devparms.channel_cnt && devparms.bandwidth && devparms.modelserie)
  {
    strlcpy(devparms.modelname, str, 128);
  }
}


void UI_Mainwindow::former_page()
{
  char str[512];

  if(device == NULL)
  {
    return;
  }

  if(!devparms.connected)
  {
    return;
  }

  if(devparms.activechannel < 0)
  {
    return;
  }

  if(devparms.timebasedelayenable)
  {
    if(devparms.timebasedelayoffset <= -(((devparms.hordivisions / 2) * devparms.timebasescale) - devparms.timebaseoffset - ((devparms.hordivisions / 2) * devparms.timebasedelayscale)))
    {
      return;
    }

    devparms.timebasedelayoffset -= devparms.timebasedelayscale * devparms.hordivisions;

    if(devparms.timebasedelayoffset <= -(((devparms.hordivisions / 2) * devparms.timebasescale) - devparms.timebaseoffset - ((devparms.hordivisions / 2) * devparms.timebasedelayscale)))
    {
      devparms.timebasedelayoffset = -(((devparms.hordivisions / 2) * devparms.timebasescale) - devparms.timebaseoffset - ((devparms.hordivisions / 2) * devparms.timebasedelayscale));
    }

    strlcpy(str, "Delayed timebase position: ", 512);

    convert_to_metric_suffix(str + strlen(str), devparms.timebasedelayoffset, 2, 512 - strlen(str));

    strlcat(str, "s", 512);

    statusLabel->setText(str);

    horPosDial_timer->start(TMC_DIAL_TIMER_DELAY);
  }
  else
  {
    devparms.timebaseoffset -= devparms.timebasescale * devparms.hordivisions;

    if(devparms.acquirememdepth > 10)
    {
      if(devparms.timebaseoffset <= -(((double)devparms.acquirememdepth / devparms.samplerate) / 2))
      {
        devparms.timebaseoffset = -(((double)devparms.acquirememdepth / devparms.samplerate) / 2);
      }
    }
    else
    {
      if(devparms.timebaseoffset <= -((devparms.hordivisions / 2) * devparms.timebasescale))
      {
        devparms.timebaseoffset = -((devparms.hordivisions / 2) * devparms.timebasescale);
      }
    }

    strlcpy(str, "Horizontal position: ", 512);

    convert_to_metric_suffix(str + strlen(str), devparms.timebaseoffset, 2, 512 - strlen(str));

    strlcat(str, "s", 512);

    statusLabel->setText(str);

    horPosDial_timer->start(TMC_DIAL_TIMER_DELAY);
  }

  waveForm->update();
}


void UI_Mainwindow::next_page()
{
  char str[512];

  if(device == NULL)
  {
    return;
  }

  if(!devparms.connected)
  {
    return;
  }

  if(devparms.activechannel < 0)
  {
    return;
  }

  if(devparms.timebasedelayenable)
  {
    if(devparms.timebasedelayoffset >= (((devparms.hordivisions / 2) * devparms.timebasescale) + devparms.timebaseoffset - ((devparms.hordivisions / 2) * devparms.timebasedelayscale)))
    {
      return;
    }

    devparms.timebasedelayoffset += devparms.timebasedelayscale * devparms.hordivisions;

    if(devparms.timebasedelayoffset >= (((devparms.hordivisions / 2) * devparms.timebasescale) + devparms.timebaseoffset - ((devparms.hordivisions / 2) * devparms.timebasedelayscale)))
    {
      devparms.timebasedelayoffset = (((devparms.hordivisions / 2) * devparms.timebasescale) + devparms.timebaseoffset - ((devparms.hordivisions / 2) * devparms.timebasedelayscale));
    }

    strlcpy(str, "Delayed timebase position: ", 512);

    convert_to_metric_suffix(str + strlen(str), devparms.timebasedelayoffset, 2, 512 - strlen(str));

    strlcat(str, "s", 512);

    statusLabel->setText(str);

    horPosDial_timer->start(TMC_DIAL_TIMER_DELAY);
  }
  else
  {
    devparms.timebaseoffset += devparms.timebasescale * devparms.hordivisions;

    if(devparms.acquirememdepth > 10)
    {
      if(devparms.timebaseoffset >= (((double)devparms.acquirememdepth / devparms.samplerate) / 2))
      {
        devparms.timebaseoffset = (((double)devparms.acquirememdepth / devparms.samplerate) / 2);
      }
    }
    else
    {
      if(devparms.timebaseoffset >= (devparms.hordivisions / 2) * devparms.timebasescale)
      {
        devparms.timebaseoffset = (devparms.hordivisions / 2) * devparms.timebasescale;
      }
    }

    strlcpy(str, "Horizontal position: ", 512);

    convert_to_metric_suffix(str + strlen(str), devparms.timebaseoffset, 2, 512 - strlen(str));

    strlcat(str, "s", 512);

    statusLabel->setText(str);

    horPosDial_timer->start(TMC_DIAL_TIMER_DELAY);
  }

  waveForm->update();
}


void UI_Mainwindow::shift_page_left()
{
  char str[512];

  if(device == NULL)
  {
    return;
  }

  if(!devparms.connected)
  {
    return;
  }

  if(devparms.math_fft && devparms.math_fft_split)
  {
    devparms.math_fft_hcenter -= devparms.math_fft_hscale;

    if(devparms.math_fft_hcenter <= 0.0)
    {
      devparms.math_fft_hcenter = 0.0;
    }

    if(devparms.modelserie != 1)
    {
      snprintf(str, 512, ":CALC:FFT:HCEN %e", devparms.math_fft_hcenter);
    }
    else
    {
      snprintf(str, 512, ":MATH:FFT:HCEN %e", devparms.math_fft_hcenter);
    }

    set_cue_cmd(str);

    strlcpy(str, "FFT center: ", 512);

    convert_to_metric_suffix(str + strlen(str), devparms.math_fft_hcenter, 0, 512 - strlen(str));

    strlcat(str, "Hz", 512);

    statusLabel->setText(str);

    waveForm->update();

    return;
  }

  if(devparms.activechannel < 0)
  {
    return;
  }

  if(devparms.timebasedelayenable)
  {
    if(devparms.timebasedelayoffset <= -(((devparms.hordivisions / 2) * devparms.timebasescale) - devparms.timebaseoffset - ((devparms.hordivisions / 2) * devparms.timebasedelayscale)))
    {
      return;
    }

    devparms.timebasedelayoffset -= devparms.timebasedelayscale;

    if(devparms.timebasedelayoffset <= -(((devparms.hordivisions / 2) * devparms.timebasescale) - devparms.timebaseoffset - ((devparms.hordivisions / 2) * devparms.timebasedelayscale)))
    {
      devparms.timebasedelayoffset = -(((devparms.hordivisions / 2) * devparms.timebasescale) - devparms.timebaseoffset - ((devparms.hordivisions / 2) * devparms.timebasedelayscale));
    }

    strlcpy(str, "Delayed timebase position: ", 512);

    convert_to_metric_suffix(str + strlen(str), devparms.timebasedelayoffset, 2, 512 - strlen(str));

    strlcat(str, "s", 512);

    statusLabel->setText(str);

    horPosDial_timer->start(TMC_DIAL_TIMER_DELAY);
  }
  else
  {
    devparms.timebaseoffset -= devparms.timebasescale;

    if(devparms.acquirememdepth > 10)
    {
      if(devparms.timebaseoffset <= -(((double)devparms.acquirememdepth / devparms.samplerate) / 2))
      {
        devparms.timebaseoffset = -(((double)devparms.acquirememdepth / devparms.samplerate) / 2);
      }
    }
    else
    {
      if(devparms.timebaseoffset <= -((devparms.hordivisions / 2) * devparms.timebasescale))
      {
        devparms.timebaseoffset = -((devparms.hordivisions / 2) * devparms.timebasescale);
      }
    }

    strlcpy(str, "Horizontal position: ", 512);

    convert_to_metric_suffix(str + strlen(str), devparms.timebaseoffset, 2, 512 - strlen(str));

    strlcat(str, "s", 512);

    statusLabel->setText(str);

    horPosDial_timer->start(TMC_DIAL_TIMER_DELAY);
  }

  waveForm->update();
}


void UI_Mainwindow::shift_page_right()
{
  char str[512];

  if(device == NULL)
  {
    return;
  }

  if(!devparms.connected)
  {
    return;
  }

  if(devparms.math_fft && devparms.math_fft_split)
  {
    devparms.math_fft_hcenter += devparms.math_fft_hscale;

    if(devparms.math_fft_hcenter >= (devparms.current_screen_sf * 0.4))
    {
      devparms.math_fft_hcenter = devparms.current_screen_sf * 0.4;
    }

    if(devparms.modelserie != 1)
    {
      snprintf(str, 512, ":CALC:FFT:HCEN %e", devparms.math_fft_hcenter);
    }
    else
    {
      snprintf(str, 512, ":MATH:FFT:HCEN %e", devparms.math_fft_hcenter);
    }

    set_cue_cmd(str);

    strlcpy(str, "FFT center: ", 512);

    convert_to_metric_suffix(str + strlen(str), devparms.math_fft_hcenter, 0, 512 - strlen(str));

    strlcat(str, "Hz", 512);

    statusLabel->setText(str);

    waveForm->update();

    return;
  }

  if(devparms.activechannel < 0)
  {
    return;
  }

  if(devparms.timebasedelayenable)
  {
    if(devparms.timebasedelayoffset >= (((devparms.hordivisions / 2) * devparms.timebasescale) + devparms.timebaseoffset - ((devparms.hordivisions / 2) * devparms.timebasedelayscale)))
    {
      return;
    }

    devparms.timebasedelayoffset += devparms.timebasedelayscale;

    if(devparms.timebasedelayoffset >= (((devparms.hordivisions / 2) * devparms.timebasescale) + devparms.timebaseoffset - ((devparms.hordivisions / 2) * devparms.timebasedelayscale)))
    {
      devparms.timebasedelayoffset = (((devparms.hordivisions / 2) * devparms.timebasescale) + devparms.timebaseoffset - ((devparms.hordivisions / 2) * devparms.timebasedelayscale));
    }

    strlcpy(str, "Delayed timebase position: ", 512);

    convert_to_metric_suffix(str + strlen(str), devparms.timebasedelayoffset, 2, 512 - strlen(str));

    strlcat(str, "s", 512);

    statusLabel->setText(str);

    horPosDial_timer->start(TMC_DIAL_TIMER_DELAY);
  }
  else
  {
    devparms.timebaseoffset += devparms.timebasescale;

    if(devparms.acquirememdepth > 10)
    {
      if(devparms.timebaseoffset >= (((double)devparms.acquirememdepth / devparms.samplerate) / 2))
      {
        devparms.timebaseoffset = (((double)devparms.acquirememdepth / devparms.samplerate) / 2);
      }
    }
    else
    {
      if(devparms.timebaseoffset >= (devparms.hordivisions / 2) * devparms.timebasescale)
      {
        devparms.timebaseoffset = (devparms.hordivisions / 2) * devparms.timebasescale;
      }
    }

    strlcpy(str, "Horizontal position: ", 512);

    convert_to_metric_suffix(str + strlen(str), devparms.timebaseoffset, 2, 512 - strlen(str));

    strlcat(str, "s", 512);

    statusLabel->setText(str);

    horPosDial_timer->start(TMC_DIAL_TIMER_DELAY);
  }

  waveForm->update();
}


void UI_Mainwindow::center_trigger()
{
  char str[512];

  if(device == NULL)
  {
    return;
  }

  if(!devparms.connected)
  {
    return;
  }

  if(devparms.math_fft && devparms.math_fft_split)
  {
    devparms.math_fft_hcenter = 0;

    if(devparms.modelserie != 1)
    {
      snprintf(str, 512, ":CALC:FFT:HCEN %e", devparms.math_fft_hcenter);
    }
    else
    {
      snprintf(str, 512, ":MATH:FFT:HCEN %e", devparms.math_fft_hcenter);
    }

    set_cue_cmd(str);

    strlcpy(str, "FFT center: ", 512);

    convert_to_metric_suffix(str + strlen(str), devparms.math_fft_hcenter, 0, 512 - strlen(str));

    strlcat(str, "Hz", 512);

    statusLabel->setText(str);

    waveForm->update();

    return;
  }

  if(devparms.activechannel < 0)
  {
    return;
  }

  if(devparms.timebasedelayenable)
  {
    devparms.timebasedelayoffset = 0;

    strlcpy(str, "Delayed timebase position: ", 512);

    convert_to_metric_suffix(str + strlen(str), devparms.timebasedelayoffset, 2, 512 - strlen(str));

    strlcat(str, "s", 512);

    statusLabel->setText(str);

    horPosDial_timer->start(TMC_DIAL_TIMER_DELAY);
  }
  else
  {
    devparms.timebaseoffset = 0;

    strlcpy(str, "Horizontal position: ", 512);

    convert_to_metric_suffix(str + strlen(str), devparms.timebaseoffset, 2, 512 - strlen(str));

    strlcat(str, "s", 512);

    statusLabel->setText(str);

    horPosDial_timer->start(TMC_DIAL_TIMER_DELAY);
  }

  waveForm->update();
}


void UI_Mainwindow::zoom_in()
{
  char str[512];

  if(device == NULL)
  {
    return;
  }

  if(!devparms.connected)
  {
    return;
  }

  if(devparms.activechannel < 0)
  {
    return;
  }

  if(devparms.math_fft && devparms.math_fft_split)
  {
    if(!dblcmp(devparms.math_fft_hscale, devparms.current_screen_sf / 200.0))
    {
      return;
    }

    if(!dblcmp(devparms.math_fft_hscale, devparms.current_screen_sf / 20.0))
    {
      devparms.math_fft_hscale = devparms.current_screen_sf / 40.0;
    }
    else if(!dblcmp(devparms.math_fft_hscale, devparms.current_screen_sf / 40.0))
      {
        devparms.math_fft_hscale = devparms.current_screen_sf / 100.0;
      }
      else
      {
        devparms.math_fft_hscale = devparms.current_screen_sf / 200.0;
      }

    if(devparms.modelserie != 1)
    {
      snprintf(str, 512, ":CALC:FFT:HSP %e", devparms.math_fft_hscale);
    }
    else
    {
      snprintf(str, 512, ":MATH:FFT:HSC %e", devparms.math_fft_hscale);
    }

    set_cue_cmd(str);

    strlcpy(str, "FFT scale: ", 512);

    convert_to_metric_suffix(str + strlen(str), devparms.math_fft_hscale, 0, 512 - strlen(str));

    strlcat(str, "Hz/Div", 512);

    statusLabel->setText(str);

    waveForm->update();

    return;
  }

  if(devparms.activechannel < 0)
  {
    return;
  }

  if(devparms.timebasedelayenable)
  {
    if(devparms.modelserie == 1)
    {
      if(devparms.timebasescale <= 5.001e-9)
      {
        devparms.timebasescale = 5e-9;

        return;
      }
    }
    else
    {
      if(devparms.bandwidth == 1000)
      {
        if(devparms.timebasedelayscale <= 5.001e-10)
        {
          devparms.timebasedelayscale = 5e-10;

          return;
        }
      }
      else
      {
        if(devparms.timebasedelayscale <= 1.001e-9)
        {
          devparms.timebasedelayscale = 1e-9;

          return;
        }
      }
    }

    devparms.timebasedelayscale = round_down_step125(devparms.timebasedelayscale, NULL);

    devparms.current_screen_sf = 100.0 / devparms.timebasedelayscale;

    strlcpy(str, "Delayed timebase: ", 512);

    convert_to_metric_suffix(str + strlen(str), devparms.timebasedelayscale, 2, 512 - strlen(str));

    strlcat(str, "s", 512);

    statusLabel->setText(str);

    snprintf(str, 512, ":TIM:DEL:SCAL %e", devparms.timebasedelayscale);

    set_cue_cmd(str);
  }
  else
  {
    if(devparms.modelserie == 1)
    {
      if(devparms.timebasescale <= 5.001e-9)
      {
        devparms.timebasescale = 5e-9;

        return;
      }
    }
    else
    {
      if(devparms.bandwidth == 1000)
      {
        if(devparms.timebasescale <= 5.001e-10)
        {
          devparms.timebasescale = 5e-10;

          return;
        }
      }
      else
      {
        if(devparms.timebasescale <= 1.001e-9)
        {
          devparms.timebasescale = 1e-9;

          return;
        }
      }
    }

    devparms.timebasescale = round_down_step125(devparms.timebasescale, NULL);

    devparms.current_screen_sf = 100.0 / devparms.timebasescale;

    strlcpy(str, "Timebase: ", 512);

    convert_to_metric_suffix(str + strlen(str), devparms.timebasescale, 2, 512 - strlen(str));

    strlcat(str, "s", 512);

    statusLabel->setText(str);

    snprintf(str, 512, ":TIM:SCAL %e", devparms.timebasescale);

    set_cue_cmd(str);
  }

  waveForm->update();
}


void UI_Mainwindow::zoom_out()
{
  char str[512];

  if(device == NULL)
  {
    return;
  }

  if(!devparms.connected)
  {
    return;
  }

  if(devparms.activechannel < 0)
  {
    return;
  }

  if(devparms.math_fft && devparms.math_fft_split)
  {
    if(!dblcmp(devparms.math_fft_hscale, devparms.current_screen_sf / 20.0))
    {
      return;
    }

    if(!dblcmp(devparms.math_fft_hscale, devparms.current_screen_sf / 200.0))
    {
      devparms.math_fft_hscale = devparms.current_screen_sf / 100.0;
    }
    else if(!dblcmp(devparms.math_fft_hscale, devparms.current_screen_sf / 100.0))
      {
        devparms.math_fft_hscale = devparms.current_screen_sf / 40.0;
      }
      else
      {
        devparms.math_fft_hscale = devparms.current_screen_sf / 20.0;
      }

    if(devparms.modelserie != 1)
    {
      snprintf(str, 512, ":CALC:FFT:HSP %e", devparms.math_fft_hscale);
    }
    else
    {
      snprintf(str, 512, ":MATH:FFT:HSC %e", devparms.math_fft_hscale);
    }

    set_cue_cmd(str);

    strlcpy(str, "FFT scale: ", 512);

    convert_to_metric_suffix(str + strlen(str), devparms.math_fft_hscale, 0, 512 - strlen(str));

    strlcat(str, "Hz/Div", 512);

    statusLabel->setText(str);

    waveForm->update();

    return;
  }

  if(devparms.timebasedelayenable)
  {
    if(devparms.timebasedelayscale >= devparms.timebasescale / 2)
    {
      devparms.timebasedelayscale = devparms.timebasescale / 2;

      return;
    }

    if(devparms.timebasedelayscale >= 0.1)
    {
      devparms.timebasedelayscale = 0.1;

      return;
    }

    devparms.timebasedelayscale = round_up_step125(devparms.timebasedelayscale, NULL);

    devparms.current_screen_sf = 100.0 / devparms.timebasedelayscale;

    strlcpy(str, "Delayed timebase: ", 512);

    convert_to_metric_suffix(str + strlen(str), devparms.timebasedelayscale, 2, 512 - strlen(str));

    strlcat(str, "s", 512);

    statusLabel->setText(str);

    snprintf(str, 512, ":TIM:DEL:SCAL %e", devparms.timebasedelayscale);

    set_cue_cmd(str);

    if(devparms.timebasedelayscale > 0.1000001)
    {
      devparms.func_wrec_enable = 0;
    }
  }
  else
  {
    if(devparms.timebasescale >= 10)
    {
      devparms.timebasescale = 10;

      return;
    }

    devparms.timebasescale = round_up_step125(devparms.timebasescale, NULL);

    devparms.current_screen_sf = 100.0 / devparms.timebasescale;

    strlcpy(str, "Timebase: ", 512);

    convert_to_metric_suffix(str + strlen(str), devparms.timebasescale, 2, 512 - strlen(str));

    strlcat(str, "s", 512);

    statusLabel->setText(str);

    snprintf(str, 512, ":TIM:SCAL %e", devparms.timebasescale);

    set_cue_cmd(str);

    if(devparms.timebasescale > 0.1000001)
    {
      devparms.func_wrec_enable = 0;
    }
  }

  waveForm->update();
}


void UI_Mainwindow::chan_scale_plus()
{
  int chn;

  double val, ltmp;

  char str[512];

  if(device == NULL)
  {
    return;
  }

  if(!devparms.connected)
  {
    return;
  }

  if(devparms.activechannel < 0)
  {
    return;
  }

  if(devparms.math_fft && devparms.math_fft_split)
  {
    devparms.fft_vscale = round_up_step125(devparms.fft_vscale, NULL);

    if(devparms.math_fft_unit == 0)
    {
      if(devparms.fft_vscale > 50.0)
      {
        devparms.fft_vscale = 50.0;
      }
    }
    else
    {
      if(devparms.fft_vscale > 20.0)
      {
        devparms.fft_vscale = 20.0;
      }
    }

    if(devparms.fft_voffset > (devparms.fft_vscale * 4.0))
    {
      devparms.fft_voffset = (devparms.fft_vscale * 4.0);
    }

    if(devparms.fft_voffset < (devparms.fft_vscale * -4.0))
    {
      devparms.fft_voffset = (devparms.fft_vscale * -4.0);
    }

    if(devparms.modelserie != 1)
    {
      if(devparms.math_fft_unit == 1)
      {
        snprintf(str, 512, ":CALC:FFT:VSC %e", devparms.fft_vscale);

        set_cue_cmd(str);
      }
      else
      {
        snprintf(str, 512, ":CALC:FFT:VSC %e", devparms.fft_vscale / devparms.chanscale[devparms.math_fft_src]);

        set_cue_cmd(str);
      }
    }
    else
    {
      snprintf(str, 512, ":MATH:SCAL %e", devparms.fft_vscale);

      set_cue_cmd(str);
    }

    if(devparms.math_fft_unit == 0)
    {
      strlcpy(str, "FFT scale: ", 512);

      convert_to_metric_suffix(str + strlen(str), devparms.fft_vscale, 1, 512 - strlen(str));

      strlcat(str, "V/Div", 512);
    }
    else
    {
      snprintf(str, 512, "FFT scale: %.1fdB/Div", devparms.fft_vscale);
    }

    statusLabel->setText(str);

    waveForm->update();

    return;
  }

  chn = devparms.activechannel;

  if(devparms.chanscale[chn] >= 20)
  {
    devparms.chanscale[chn] = 20;

    return;
  }

  ltmp = devparms.chanscale[chn];

  val = round_up_step125(devparms.chanscale[chn], NULL);

  if(devparms.chanvernier[chn])
  {
    val /= 100;

    devparms.chanscale[chn] += val;
  }
  else
  {
    devparms.chanscale[chn] = val;
  }

  ltmp /= val;

  devparms.chanoffset[chn] /= ltmp;

  snprintf(str, 512, "Channel %i scale: ", chn + 1);

  convert_to_metric_suffix(str + strlen(str), devparms.chanscale[chn], 2, 512 - strlen(str));

  strlcat(str, "V", 512);

  statusLabel->setText(str);

  snprintf(str, 512, ":CHAN%i:SCAL %e", chn + 1, devparms.chanscale[chn]);

  set_cue_cmd(str);

  waveForm->update();
}


void UI_Mainwindow::chan_scale_plus_all()
{
  int chn;

  double val, ltmp;

  char str[512];

  if(device == NULL)
  {
    return;
  }

  if(!devparms.connected)
  {
    return;
  }

  if(devparms.activechannel < 0)
  {
    return;
  }

  if(devparms.math_fft && devparms.math_fft_split)
  {
    return;
  }

  for(chn=0; chn<devparms.channel_cnt; chn++)
  {
    if(!devparms.chandisplay[chn])  continue;

    if(devparms.chanscale[chn] >= 20)
    {
      devparms.chanscale[chn] = 20;

      return;
    }

    ltmp = devparms.chanscale[chn];

    val = round_up_step125(devparms.chanscale[chn], NULL);

    if(devparms.chanvernier[chn])
    {
      val /= 100;

      devparms.chanscale[chn] += val;
    }
    else
    {
      devparms.chanscale[chn] = val;
    }

    ltmp /= val;

    devparms.chanoffset[chn] /= ltmp;

    snprintf(str, 512, "Channel %i scale: ", chn + 1);

    convert_to_metric_suffix(str + strlen(str), devparms.chanscale[chn], 2, 512 - strlen(str));

    strlcat(str, "V", 512);

    statusLabel->setText(str);

    snprintf(str, 512, ":CHAN%i:SCAL %e", chn + 1, devparms.chanscale[chn]);

    set_cue_cmd(str);
  }

  waveForm->update();
}


void UI_Mainwindow::chan_scale_minus()
{
  int chn;

  double val, ltmp;

  char str[512];

  if(device == NULL)
  {
    return;
  }

  if(!devparms.connected)
  {
    return;
  }

  if(devparms.activechannel < 0)
  {
    return;
  }

  if(devparms.math_fft && devparms.math_fft_split)
  {
    devparms.fft_vscale = round_down_step125(devparms.fft_vscale, NULL);

    if(devparms.math_fft_unit == 0)
    {
      if(devparms.fft_vscale < 1.0)
      {
        devparms.fft_vscale = 1.0;
      }
    }
    else
    {
      if(devparms.fft_vscale < 1.0)
      {
        devparms.fft_vscale = 1.0;
      }
    }

    if(devparms.fft_voffset > (devparms.fft_vscale * 4.0))
    {
      devparms.fft_voffset = (devparms.fft_vscale * 4.0);
    }

    if(devparms.fft_voffset < (devparms.fft_vscale * -4.0))
    {
      devparms.fft_voffset = (devparms.fft_vscale * -4.0);
    }

    if(devparms.modelserie != 1)
    {
      if(devparms.math_fft_unit == 1)
      {
        snprintf(str, 512, ":CALC:FFT:VSC %e", devparms.fft_vscale);

        set_cue_cmd(str);
      }
      else
      {
        snprintf(str, 512, ":CALC:FFT:VSC %e", devparms.fft_vscale / devparms.chanscale[devparms.math_fft_src]);

        set_cue_cmd(str);
      }
    }
    else
    {
      snprintf(str, 512, ":MATH:SCAL %e", devparms.fft_vscale);

      set_cue_cmd(str);
    }

    if(devparms.math_fft_unit == 0)
    {
      strlcpy(str, "FFT scale: ", 512);

      convert_to_metric_suffix(str + strlen(str), devparms.fft_vscale, 1, 512 - strlen(str));

      strlcat(str, "V/Div", 512);
    }
    else
    {
      snprintf(str, 512, "FFT scale: %.1fdB/Div", devparms.fft_vscale);
    }

    statusLabel->setText(str);

    waveForm->update();

    return;
  }

  chn = devparms.activechannel;

  if(devparms.chanscale[chn] <= 1e-2)
  {
    devparms.chanscale[chn] = 1e-2;

    return;
  }

  ltmp = devparms.chanscale[chn];

  if(devparms.chanvernier[chn])
  {
    val = round_up_step125(devparms.chanscale[chn], NULL);
  }
  else
  {
    val = round_down_step125(devparms.chanscale[chn], NULL);
  }

  if(devparms.chanvernier[chn])
  {
    val /= 100;

    devparms.chanscale[chn] -= val;
  }
  else
  {
    devparms.chanscale[chn] = val;
  }

  ltmp /= val;

  devparms.chanoffset[chn] /= ltmp;

  snprintf(str, 512, "Channel %i scale: ", chn + 1);

  convert_to_metric_suffix(str + strlen(str), devparms.chanscale[chn], 2, 512 - strlen(str));

  strlcat(str, "V", 512);

  statusLabel->setText(str);

  snprintf(str, 512, ":CHAN%i:SCAL %e", chn + 1, devparms.chanscale[chn]);

  set_cue_cmd(str);

  waveForm->update();
}


void UI_Mainwindow::chan_scale_minus_all()
{
  int chn;

  double val, ltmp;

  char str[512];

  if(device == NULL)
  {
    return;
  }

  if(!devparms.connected)
  {
    return;
  }

  if(devparms.activechannel < 0)
  {
    return;
  }

  if(devparms.math_fft && devparms.math_fft_split)
  {
    return;
  }

  for(chn=0; chn<devparms.channel_cnt; chn++)
  {
    if(!devparms.chandisplay[chn])  continue;

    if(devparms.chanscale[chn] <= 1e-2)
    {
      devparms.chanscale[chn] = 1e-2;

      return;
    }

    ltmp = devparms.chanscale[chn];

    if(devparms.chanvernier[chn])
    {
      val = round_up_step125(devparms.chanscale[chn], NULL);
    }
    else
    {
      val = round_down_step125(devparms.chanscale[chn], NULL);
    }

    if(devparms.chanvernier[chn])
    {
      val /= 100;

      devparms.chanscale[chn] -= val;
    }
    else
    {
      devparms.chanscale[chn] = val;
    }

    ltmp /= val;

    devparms.chanoffset[chn] /= ltmp;

    snprintf(str, 512, "Channel %i scale: ", chn + 1);

    convert_to_metric_suffix(str + strlen(str), devparms.chanscale[chn], 2, 512 - strlen(str));

    strlcat(str, "V", 512);

    statusLabel->setText(str);

    snprintf(str, 512, ":CHAN%i:SCAL %e", chn + 1, devparms.chanscale[chn]);

    set_cue_cmd(str);
  }

  waveForm->update();
}


void UI_Mainwindow::shift_trace_up()
{
  int chn;

  char str[512];

  if(device == NULL)
  {
    return;
  }

  if(!devparms.connected)
  {
    return;
  }

  if(devparms.activechannel < 0)
  {
    return;
  }

  if(devparms.math_fft && devparms.math_fft_split)
  {
    devparms.fft_voffset += devparms.fft_vscale;

    if(devparms.fft_voffset > (devparms.fft_vscale * 4.0))
    {
      devparms.fft_voffset = (devparms.fft_vscale * 4.0);
    }

    if(devparms.math_fft_unit && (devparms.fft_vscale > 9.9))
    {
      devparms.fft_voffset = nearbyint(devparms.fft_voffset);
    }

    if(devparms.modelserie != 1)
    {
      snprintf(str, 512, ":CALC:FFT:VOFF %e", devparms.fft_voffset);

      set_cue_cmd(str);
    }
    else
    {
      snprintf(str, 512, ":MATH:OFFS %e", devparms.fft_voffset);

      set_cue_cmd(str);
    }

    if(devparms.math_fft_unit == 0)
    {
      strlcpy(str, "FFT position: ", 512);

      convert_to_metric_suffix(str + strlen(str), devparms.fft_voffset, 1, 512 - strlen(str));

      strlcat(str, "V", 512);
    }
    else
    {
      snprintf(str, 512, "FFT position: %+.0fdB", devparms.fft_voffset);
    }

    statusLabel->setText(str);

    waveForm->label_active = LABEL_ACTIVE_FFT;

    label_timer->start(LABEL_TIMER_IVAL);

    waveForm->update();

    return;
  }

  chn = devparms.activechannel;

  if(devparms.chanoffset[chn] >= 20)
  {
    devparms.chanoffset[chn] = 20;

    return;
  }

  devparms.chanoffset[chn] += devparms.chanscale[chn];

  snprintf(str, 512, "Channel %i offset: ", chn + 1);

  convert_to_metric_suffix(str + strlen(str), devparms.chanoffset[chn], 2, 512 - strlen(str));

  strlcat(str, devparms.chanunitstr[devparms.chanunit[chn]], 512);

  statusLabel->setText(str);

  waveForm->label_active = chn + 1;

  label_timer->start(LABEL_TIMER_IVAL);

  vertOffsDial_timer->start(TMC_DIAL_TIMER_DELAY);

  waveForm->update();
}


void UI_Mainwindow::shift_trace_down()
{
  int chn;

  char str[512];

  if(device == NULL)
  {
    return;
  }

  if(!devparms.connected)
  {
    return;
  }

  if(devparms.activechannel < 0)
  {
    return;
  }

  if(devparms.math_fft && devparms.math_fft_split)
  {
    devparms.fft_voffset -= devparms.fft_vscale;

    if(devparms.fft_voffset < (devparms.fft_vscale * -4.0))
    {
      devparms.fft_voffset = (devparms.fft_vscale * -4.0);
    }

    if(devparms.math_fft_unit)
    {
      if(devparms.fft_vscale > 9.0)
      {
        devparms.fft_voffset = nearbyint(devparms.fft_voffset);
      }
      else
      {
        devparms.fft_voffset = nearbyint(devparms.fft_voffset * 10.0) / 10.0;
      }
    }

    if(devparms.modelserie != 1)
    {
      snprintf(str, 512, ":CALC:FFT:VOFF %e", devparms.fft_voffset);

      set_cue_cmd(str);
    }
    else
    {
      snprintf(str, 512, ":MATH:OFFS %e", devparms.fft_voffset);

      set_cue_cmd(str);
    }

    if(devparms.math_fft_unit == 0)
    {
      strlcpy(str, "FFT position: ", 512);

      convert_to_metric_suffix(str + strlen(str), devparms.fft_voffset, 1, 512 - strlen(str));

      strlcat(str, "V", 512);
    }
    else
    {
      snprintf(str, 512, "FFT position: %+.0fdB", devparms.fft_voffset);
    }

    statusLabel->setText(str);

    waveForm->label_active = LABEL_ACTIVE_FFT;

    label_timer->start(LABEL_TIMER_IVAL);

    waveForm->update();

    return;
  }

  chn = devparms.activechannel;

  if(devparms.chanoffset[chn] <= -20)
  {
    devparms.chanoffset[chn] = -20;

    return;
  }

  devparms.chanoffset[chn] -= devparms.chanscale[chn];

  snprintf(str, 512, "Channel %i offset: ", chn + 1);

  convert_to_metric_suffix(str + strlen(str), devparms.chanoffset[chn], 2, 512 - strlen(str));

  strlcat(str, devparms.chanunitstr[devparms.chanunit[chn]], 512);

  statusLabel->setText(str);

  waveForm->label_active = chn + 1;

  label_timer->start(LABEL_TIMER_IVAL);

  vertOffsDial_timer->start(TMC_DIAL_TIMER_DELAY);

  waveForm->update();
}


void UI_Mainwindow::set_to_factory()
{
  int i;

  char str[512];

  if((device == NULL) || (!devparms.connected))
  {
    return;
  }

  QMessageBox msgBox2;
  msgBox2.setText("Do you want to reset the instrument to the factory settings?");
  msgBox2.setStandardButtons(QMessageBox::No | QMessageBox::Yes);
  msgBox2.setDefaultButton(QMessageBox::No);
  if(msgBox2.exec() != QMessageBox::Yes)  return;

  scrn_timer->stop();

  scrn_thread->wait();

  tmc_write("*RST");

  devparms.timebasescale = 1e-6;

  devparms.timebaseoffset = 0;

  devparms.timebasedelayenable = 0;

  devparms.timebasedelayoffset = 0;

  for(i=0; i<MAX_CHNS; i++)
  {
    devparms.chanscale[i] = 1;

    devparms.chanoffset[i] = 0;

    devparms.chandisplay[i] = 0;

    devparms.chancoupling[i] = 1;

    devparms.chanbwlimit[i] = 0;

    devparms.chanprobe[i] = 10;

    devparms.chaninvert[i] = 0;

    devparms.chanvernier[i] = 0;

    devparms.triggeredgelevel[i] = 0;
  }

  devparms.chandisplay[0] = 1;

  devparms.activechannel = 0;

  devparms.acquiretype = 0;

  devparms.acquirememdepth = 0;

  devparms.triggermode = 0;

  devparms.triggeredgesource = 0;

  devparms.triggeredgeslope = 0;

  devparms.triggerstatus = 3;

  devparms.triggercoupling = 1;

  if(devparms.modelserie == 1)
  {
    devparms.triggerholdoff = 1.6e-8;
  }
  else
  {
    devparms.triggerholdoff = 1e-7;
  }

  devparms.displaytype = 0;

  devparms.displaygrid = 2;

  devparms.countersrc = 0;

  devparms.func_wrec_enable = 0;

  statusLabel->setText("Reset to factory settings");

  waveForm->update();

  QMessageBox msgBox;
  msgBox.setText("Resetting the instrument to the factory settings.\n"
                 "Please wait...");

  QTimer t_rst_1;
  t_rst_1.setSingleShot(true);
#if QT_VERSION >= 0x050000
  t_rst_1.setTimerType(Qt::PreciseTimer);
#endif
  connect(&t_rst_1, SIGNAL(timeout()), &msgBox, SLOT(accept()));
  t_rst_1.start(9000);

  msgBox.exec();

  if(devparms.modelserie == 6)
  {
    for(i=0; i<MAX_CHNS; i++)
    {
      snprintf(str, 512, ":CHAN%i:SCAL 1", i + 1);

      tmc_write(str);

      usleep(20000);
    }
  }

  scrn_timer->start(devparms.screentimerival);
}


// this function is called when screen_thread has finished
void UI_Mainwindow::screenUpdate()
{
  int i, chns=0;

  char str[512];

  if(device == NULL)
  {
    pthread_mutex_unlock(&devparms.mutexx);

    return;
  }

  if(!devparms.connected)
  {
    pthread_mutex_unlock(&devparms.mutexx);

    return;
  }

  if(!devparms.screenupdates_on)
  {
    pthread_mutex_unlock(&devparms.mutexx);

    return;
  }

  scrn_thread->get_params(&devparms);

  if(devparms.thread_error_stat)
  {
    scrn_timer->stop();

    snprintf(str, 512, "An error occurred while reading screen data from device.\n"
                "File screen_thread.cpp line %i", devparms.thread_error_line);

    QMessageBox msgBox;
    msgBox.setIcon(QMessageBox::Critical);
    msgBox.setText(str);
    msgBox.exec();

    pthread_mutex_unlock(&devparms.mutexx);

    close_connection();

    return;
  }

  if(devparms.thread_result == TMC_THRD_RESULT_NONE)
  {
    pthread_mutex_unlock(&devparms.mutexx);

    return;
  }

  if(devparms.thread_result == TMC_THRD_RESULT_CMD)
  {
    if(devparms.thread_job == TMC_THRD_JOB_TRIGEDGELEV)
    {
      devparms.triggeredgelevel[devparms.triggeredgesource] = devparms.thread_value;

//      waveForm->setTrigLineVisible();
    }

    pthread_mutex_unlock(&devparms.mutexx);

    return;
  }

  if(scrn_timer->isActive() == false)
  {
    pthread_mutex_unlock(&devparms.mutexx);

    return;
  }

  runButton->setStyleSheet(def_stylesh);

  singleButton->setStyleSheet(def_stylesh);

  if(devparms.triggerstatus == 0)
  {
    runButton->setStyleSheet("background: #66FF99;");
  }
  else if(devparms.triggerstatus == 1)
    {
      singleButton->setStyleSheet("background: #FF9966;");
    }
    else if(devparms.triggerstatus == 2)
      {
        runButton->setStyleSheet("background: #66FF99;");
      }
      else if(devparms.triggerstatus == 3)
        {
          runButton->setStyleSheet("background: #66FF99;");
        }
        else if(devparms.triggerstatus == 5)
          {
            runButton->setStyleSheet("background: #FF0066;");
          }

  if(devparms.triggersweep == 0)
  {
    trigModeAutoLed->setValue(true);
    trigModeNormLed->setValue(false);
    trigModeSingLed->setValue(false);
  }
  else if(devparms.triggersweep == 1)
    {
      trigModeAutoLed->setValue(false);
      trigModeNormLed->setValue(true);
      trigModeSingLed->setValue(false);
    }
    else if(devparms.triggersweep == 2)
      {
        trigModeAutoLed->setValue(false);
        trigModeNormLed->setValue(false);
        trigModeSingLed->setValue(true);
      }

  if(waveForm->hasMoveEvent() == true)
  {
    pthread_mutex_unlock(&devparms.mutexx);

    return;
  }

  for(i=0; i<MAX_CHNS; i++)
  {
    if(!devparms.chandisplay[i])  // Display data only when channel is switched on
    {
      continue;
    }

    chns++;
  }

  if(!chns)
  {
    waveForm->clear();

    pthread_mutex_unlock(&devparms.mutexx);

    return;
  }

//  if(devparms.triggerstatus != 1)  // Don't plot waveform data when triggerstatus is "wait"
  if(1)
  {
    if(devparms.math_decode_display)
    {
      serial_decoder(&devparms);
    }

    waveForm->drawCurve(&devparms, device);
  }
  else  // trigger status is "wait"
  {
    waveForm->update();
  }

  pthread_mutex_unlock(&devparms.mutexx);
}


void UI_Mainwindow::set_cue_cmd(const char *str)
{
  strlcpy(devparms.cmd_cue[devparms.cmd_cue_idx_in], str, 128);

  devparms.cmd_cue_resp[devparms.cmd_cue_idx_in] = NULL;

  devparms.cmd_cue_idx_in++;

  devparms.cmd_cue_idx_in %= TMC_CMD_CUE_SZ;

  scrn_timer_handler();
}


void UI_Mainwindow::set_cue_cmd(const char *str, char *ptr)
{
  strlcpy(devparms.cmd_cue[devparms.cmd_cue_idx_in], str, 128);

  ptr[0] = 0;

  devparms.cmd_cue_resp[devparms.cmd_cue_idx_in] = ptr;

  devparms.cmd_cue_idx_in++;

  devparms.cmd_cue_idx_in %= TMC_CMD_CUE_SZ;

  scrn_timer_handler();
}














