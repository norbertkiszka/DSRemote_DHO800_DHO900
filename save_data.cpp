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


#define SAV_MEM_BSZ    (250000)



void UI_Mainwindow::save_app_screenshot()
{
  char opath[MAX_PATHLEN]="";

  if(device == NULL)
  {
    return;
  }

  scrn_timer->stop();

  scrn_thread->wait();

  if(recent_savedir[0]!=0)
  {
    strlcpy(opath, recent_savedir, MAX_PATHLEN);
    strlcat(opath, "/", MAX_PATHLEN);
  }
  strlcat(opath, "screenshot.png", MAX_PATHLEN);

  strlcpy(opath, QFileDialog::getSaveFileName(this, "Save file", opath, "PNG files (*.png *.PNG)").toLocal8Bit().data(), MAX_PATHLEN);

  if(!strcmp(opath, ""))
  {
    scrn_timer->start(devparms.screentimerival);

    return;
  }

  get_directory_from_path(recent_savedir, opath, MAX_PATHLEN);

  waveForm->print_to_image(opath);

  scrn_timer->start(devparms.screentimerival);
}


void UI_Mainwindow::save_screenshot()
{
  int n;

  char str[512]="",
       opath[MAX_PATHLEN]="";

  QPainter painter;

  QPainterPath path;

  if(device == NULL)
  {
    return;
  }

  scrn_timer->stop();

  scrn_thread->wait();

  tmc_write(":DISPlay:DATA?");

  save_data_thread get_data_thrd(0);

  QMessageBox w_msg_box;
  w_msg_box.setIcon(QMessageBox::NoIcon);
  w_msg_box.setText("Downloading data...");
  w_msg_box.setStandardButtons(QMessageBox::Abort);

  connect(&get_data_thrd, SIGNAL(finished()), &w_msg_box, SLOT(accept()));

  get_data_thrd.start();

  if(w_msg_box.exec() != QDialog::Accepted)
  {
    disconnect(&get_data_thrd, 0, 0, 0);
    strlcpy(str, "Aborted by user.", 512);
    goto OUT_ERROR;
  }

  disconnect(&get_data_thrd, 0, 0, 0);

  n = get_data_thrd.get_num_bytes_rcvd();
  if(n < 0)
  {
    strlcpy(str, "Can not read from device.", 512);
    goto OUT_ERROR;
  }

  if(device->sz != SCRN_SHOT_BMP_SZ)
  {
    strlcpy(str, "Error, bitmap has wrong filesize\n", 512);
    goto OUT_ERROR;
  }

  if(strncmp(device->buf, "BM", 2))
  {
    strlcpy(str, "Error, file is not a bitmap\n", 512);
    goto OUT_ERROR;
  }

  memcpy(devparms.screenshot_buf, device->buf, SCRN_SHOT_BMP_SZ);

  screenXpm.loadFromData((uchar *)(devparms.screenshot_buf), SCRN_SHOT_BMP_SZ);

  if(devparms.modelserie == 1)
  {
    painter.begin(&screenXpm);
#if (QT_VERSION >= 0x050000) && (QT_VERSION < 0x060000)
    painter.setRenderHint(QPainter::Qt4CompatiblePainting, true);
#endif

    painter.fillRect(0, 0, 80, 29, Qt::black);

    painter.setPen(Qt::white);

    painter.drawText(5, 8, 65, 20, Qt::AlignCenter, devparms.modelname);

    painter.end();
  }
  else if(devparms.modelserie == 6)
    {
      painter.begin(&screenXpm);
#if (QT_VERSION >= 0x050000) && (QT_VERSION < 0x060000)
      painter.setRenderHint(QPainter::Qt4CompatiblePainting, true);
#endif

      painter.fillRect(0, 0, 95, 29, QColor(48, 48, 48));

      path.addRoundedRect(5, 5, 85, 20, 3, 3);

      painter.fillPath(path, Qt::black);

      painter.setPen(Qt::white);

      painter.drawText(5, 5, 85, 20, Qt::AlignCenter, devparms.modelname);

      painter.end();
    }

  if(devparms.screenshot_inv)
  {
    screenXpm.invertPixels(QImage::InvertRgb);
  }

  opath[0] = 0;
  if(recent_savedir[0]!=0)
  {
    strlcpy(opath, recent_savedir, MAX_PATHLEN);
    strlcat(opath, "/", MAX_PATHLEN);
  }
  strlcat(opath, "screenshot.png", MAX_PATHLEN);

  strlcpy(opath, QFileDialog::getSaveFileName(this, "Save file", opath, "PNG files (*.png *.PNG)").toLocal8Bit().data(), MAX_PATHLEN);

  if(!strcmp(opath, ""))
  {
    scrn_timer->start(devparms.screentimerival);

    return;
  }

  get_directory_from_path(recent_savedir, opath, MAX_PATHLEN);

  if(screenXpm.save(QString::fromLocal8Bit(opath), "PNG", 50) == false)
  {
    strlcpy(str, "Could not save file (unknown error)", 512);
    goto OUT_ERROR;
  }

  scrn_timer->start(devparms.screentimerival);

  return;

OUT_ERROR:

  if(get_data_thrd.isFinished() != true)
  {
    connect(&get_data_thrd, SIGNAL(finished()), &w_msg_box, SLOT(accept()));
    w_msg_box.setText("Waiting for thread to finish, please wait...");
    w_msg_box.exec();
  }

  scrn_timer->start(devparms.screentimerival);

  QMessageBox msgBox;
  msgBox.setIcon(QMessageBox::Critical);
  msgBox.setText(str);
  msgBox.exec();
}


void UI_Mainwindow::get_deep_memory_waveform(void)
{
  int i, k,
      n=0,
      chn,
      chns=0,
      bytes_rcvd=0,
      mempnts,
      yref[MAX_CHNS],
      empty_buf;

  char str[512];

  short *wavbuf[MAX_CHNS];

  QEventLoop ev_loop;

  QMessageBox wi_msg_box;

  save_data_thread get_data_thrd(0);

  if(device == NULL)
  {
    return;
  }

  scrn_timer->stop();

  scrn_thread->wait();

  for(i=0; i<MAX_CHNS; i++)
  {
    wavbuf[i] = NULL;
  }

  mempnts = devparms.acquirememdepth;

  QProgressDialog progress("Downloading data...", "Abort", 0, mempnts, this);
  progress.setWindowModality(Qt::WindowModal);
  progress.setMinimumDuration(0);

  connect(&get_data_thrd, SIGNAL(finished()), &ev_loop, SLOT(quit()));

  statusLabel->setText("Downloading data...");

  for(i=0; i<MAX_CHNS; i++)
  {
    if(!devparms.chandisplay[i])
    {
      continue;
    }

    chns++;
  }

  if(!chns)
  {
    strlcpy(str, "No active channels.", 512);
    goto OUT_ERROR;
  }

  if(mempnts < 1)
  {
    strlcpy(str, "Can not download waveform when memory depth is set to \"Auto\".", 512);
    goto OUT_ERROR;
  }

  for(i=0; i<MAX_CHNS; i++)
  {
    if(!devparms.chandisplay[i])  // Download data only when channel is switched on
    {
      continue;
    }

    wavbuf[i] = (short *)malloc(mempnts * sizeof(short));
    if(wavbuf[i] == NULL)
    {
      snprintf(str, 512, "Malloc error.  line %i file %s", __LINE__, __FILE__);
      goto OUT_ERROR;
    }
  }

  tmc_write(":STOP");

  usleep(20000);

  for(chn=0; chn<MAX_CHNS; chn++)
  {
    if(!devparms.chandisplay[chn])  // Download data only when channel is switched on
    {
      continue;
    }

    snprintf(str, 512, "Downloading channel %i waveform data...", chn + 1);
    progress.setLabelText(str);

    snprintf(str, 512, ":WAV:SOUR CHAN%i", chn + 1);

    tmc_write(str);

    tmc_write(":WAV:FORM BYTE");

    usleep(20000);

    tmc_write(":WAV:MODE RAW");

    usleep(20000);

    tmc_write(":WAV:YINC?");

    usleep(20000);

    tmc_read();

    devparms.yinc[chn] = atof(device->buf);

    if(devparms.yinc[chn] < 1e-6)
    {
      snprintf(str, 512, "Error, parameter \"YINC\" out of range for channel %i: %e  line %i file %s", chn, devparms.yinc[chn], __LINE__, __FILE__);
      goto OUT_ERROR;
    }

    usleep(20000);

    tmc_write(":WAV:YREF?");

    usleep(20000);

    tmc_read();

    yref[chn] = atoi(device->buf);

    if((yref[chn] < 1) || (yref[chn] > 255))
    {
      snprintf(str, 512, "Error, parameter \"YREF\" out of range for channel %i: %i  line %i file %s", chn, yref[chn], __LINE__, __FILE__);
      goto OUT_ERROR;
    }

    usleep(20000);

    tmc_write(":WAV:YOR?");

    usleep(20000);

    tmc_read();

    devparms.yor[chn] = atoi(device->buf);

    if((devparms.yor[chn] < -32000) || (devparms.yor[chn] > 32000))
    {
      snprintf(str, 512, "Error, parameter \"YOR\" out of range for channel %i: %i  line %i file %s", chn, devparms.yor[chn], __LINE__, __FILE__);
      goto OUT_ERROR;
    }

//     printf("yinc[%i] : %f\n", chn, devparms.yinc[chn]);
//     printf("yref[%i] : %i\n", chn, yref[chn]);
//     printf("yor[%i]  : %i\n", chn, devparms.yor[chn]);

    empty_buf = 0;

    for(bytes_rcvd=0; bytes_rcvd<mempnts ;)
    {
      progress.setValue(bytes_rcvd);

      if(progress.wasCanceled())
      {
        strlcpy(str, "Canceled", 512);
        goto OUT_ERROR;
      }

      snprintf(str, 512, ":WAV:STAR %i",  bytes_rcvd + 1);

      usleep(20000);

      tmc_write(str);

      if((bytes_rcvd + SAV_MEM_BSZ) > mempnts)
      {
        snprintf(str, 512, ":WAV:STOP %i", mempnts);
      }
      else
      {
        snprintf(str, 512, ":WAV:STOP %i", bytes_rcvd + SAV_MEM_BSZ);
      }

      usleep(20000);

      tmc_write(str);

      usleep(20000);

      tmc_write(":WAV:DATA?");

      get_data_thrd.start();

      ev_loop.exec();

      n = get_data_thrd.get_num_bytes_rcvd();
      if(n < 0)
      {
        snprintf(str, 512, "Can not read from device.  line %i file %s", __LINE__, __FILE__);
        goto OUT_ERROR;
      }

      if(n == 0)
      {
        snprintf(str, 512, "No waveform data available.");
        goto OUT_ERROR;
      }

      printf("received %i bytes, total %i bytes\n", n, n + bytes_rcvd);

      if(n > SAV_MEM_BSZ)
      {
        snprintf(str, 512, "Datablock too big for buffer: %i  line %i file %s", n, __LINE__, __FILE__);
        goto OUT_ERROR;
      }

      if(n < 1)
      {
        if(empty_buf++ > 100)
        {
          break;
        }
      }
      else
      {
        empty_buf = 0;
      }

      for(k=0; k<n; k++)
      {
        if((bytes_rcvd + k) >= mempnts)
        {
          break;
        }

        wavbuf[chn][bytes_rcvd + k] = ((int)(((unsigned char *)device->buf)[k])) - yref[chn] - devparms.yor[chn];
      }

      bytes_rcvd += n;

      if(bytes_rcvd >= mempnts)
      {
        break;
      }
    }

    if(bytes_rcvd < mempnts)
    {
      snprintf(str, 512, "Download error.  line %i file %s", __LINE__, __FILE__);
      goto OUT_ERROR;
    }
  }

  progress.reset();

  for(chn=0; chn<MAX_CHNS; chn++)
  {
    if(!devparms.chandisplay[chn])
    {
      continue;
    }

    snprintf(str, 512, ":WAV:SOUR CHAN%i", chn + 1);

    usleep(20000);

    tmc_write(str);

    usleep(20000);

    tmc_write(":WAV:MODE NORM");

    usleep(20000);

    tmc_write(":WAV:STAR 1");

    if(devparms.modelserie == 1)
    {
      usleep(20000);

      tmc_write(":WAV:STOP 1200");
    }
    else
    {
      usleep(20000);

      tmc_write(":WAV:STOP 1400");

      usleep(20000);

      tmc_write(":WAV:POIN 1400");
    }
  }

  if(bytes_rcvd < mempnts)
  {
    snprintf(str, 512, "Download error.  line %i file %s", __LINE__, __FILE__);
    goto OUT_ERROR;
  }
  else
  {
    statusLabel->setText("Downloading finished");
  }

  new UI_wave_window(&devparms, wavbuf, this);

  disconnect(&get_data_thrd, 0, 0, 0);

  scrn_timer->start(devparms.screentimerival);

  return;

OUT_ERROR:

  disconnect(&get_data_thrd, 0, 0, 0);

  progress.reset();

  statusLabel->setText("Downloading aborted");

  if(get_data_thrd.isRunning() == true)
  {
    QMessageBox w_msg_box;
    w_msg_box.setIcon(QMessageBox::NoIcon);
    w_msg_box.setText("Waiting for thread to finish, please wait...");
    w_msg_box.setStandardButtons(QMessageBox::Abort);

    connect(&get_data_thrd, SIGNAL(finished()), &w_msg_box, SLOT(accept()));

    w_msg_box.exec();
  }

  if(progress.wasCanceled() == false)
  {
    QMessageBox msgBox;
    msgBox.setIcon(QMessageBox::Critical);
    msgBox.setText(str);
    msgBox.exec();
  }

  for(chn=0; chn<MAX_CHNS; chn++)
  {
    if(!devparms.chandisplay[chn])
    {
      continue;
    }

    snprintf(str, 512, ":WAV:SOUR CHAN%i", chn + 1);

    tmc_write(str);

    tmc_write(":WAV:MODE NORM");

    tmc_write(":WAV:STAR 1");

    if(devparms.modelserie == 1)
    {
      tmc_write(":WAV:STOP 1200");
    }
    else
    {
      tmc_write(":WAV:STOP 1400");

      tmc_write(":WAV:POIN 1400");
    }
  }

  for(chn=0; chn<MAX_CHNS; chn++)
  {
    free(wavbuf[chn]);
    wavbuf[chn] = NULL;
  }

  scrn_timer->start(devparms.screentimerival);
}


void UI_Mainwindow::save_wave_inspector_buffer_to_edf(struct device_settings *d_parms)
{
  int i, j,
      chn,
      chns=0,
      hdl=-1,
      mempnts,
      smps_per_record,
      datrecs=1,
      ret_stat;

  char str[512],
       opath[MAX_PATHLEN];

  long long rec_len=0LL,
            datrecduration;

  QMessageBox wi_msg_box;

  save_data_thread sav_data_thrd(1);

  mempnts = d_parms->acquirememdepth;

  smps_per_record = mempnts;

  for(i=0; i<MAX_CHNS; i++)
  {
    if(!d_parms->chandisplay[i])
    {
      continue;
    }

    chns++;
  }

  if(!chns)
  {
    strlcpy(str, "No active channels.", 512);
    goto OUT_ERROR;
  }

  while(smps_per_record >= (5000000 / chns))
  {
    smps_per_record /= 2;

    datrecs *= 2;
  }

  rec_len = (EDFLIB_TIME_DIMENSION * (long long)mempnts) / d_parms->samplerate;

  if(rec_len < 100)
  {
    strlcpy(str, "Can not save waveforms shorter than 10 uSec.\n"
                "Select a higher memory depth or a higher timebase.", 512);
    goto OUT_ERROR;
  }

  opath[0] = 0;
  if(recent_savedir[0]!=0)
  {
    strlcpy(opath, recent_savedir, MAX_PATHLEN);
    strlcat(opath, "/", MAX_PATHLEN);
  }
  strlcat(opath, "waveform.edf", MAX_PATHLEN);

  strlcpy(opath, QFileDialog::getSaveFileName(this, "Save file", opath, "EDF files (*.edf *.EDF)").toLocal8Bit().data(), MAX_PATHLEN);

  if(!strcmp(opath, ""))
  {
    goto OUT_NORMAL;
  }

  get_directory_from_path(recent_savedir, opath, MAX_PATHLEN);

  hdl = edfopen_file_writeonly(opath, EDFLIB_FILETYPE_EDFPLUS, chns);
  if(hdl < 0)
  {
    strlcpy(str, "Can not create EDF file.", 512);
    goto OUT_ERROR;
  }

  statusLabel->setText("Saving EDF file...");

  datrecduration = (rec_len / 10LL) / datrecs;

//  printf("rec_len: %lli   datrecs: %i   datrecduration: %lli\n", rec_len, datrecs, datrecduration);

  if(datrecduration < 10000LL)
  {
    if(edf_set_micro_datarecord_duration(hdl, datrecduration))
    {
      snprintf(str, 512, "Can not set datarecord duration of EDF file: %lli", datrecduration);
      printf("\ndebug line %i: rec_len: %lli   datrecs: %i   datrecduration: %lli\n", __LINE__, rec_len, datrecs, datrecduration);
      goto OUT_ERROR;
    }
  }
  else
  {
    if(edf_set_datarecord_duration(hdl, datrecduration / 10LL))
    {
      snprintf(str, 512, "Can not set datarecord duration of EDF file: %lli", datrecduration);
      printf("\ndebug line %i: rec_len: %lli   datrecs: %i   datrecduration: %lli\n", __LINE__, rec_len, datrecs, datrecduration);
      goto OUT_ERROR;
    }
  }

  j = 0;

  for(chn=0; chn<MAX_CHNS; chn++)
  {
    if(!d_parms->chandisplay[chn])
    {
      continue;
    }

    edf_set_samplefrequency(hdl, j, smps_per_record);
    edf_set_digital_maximum(hdl, j, 32767);
    edf_set_digital_minimum(hdl, j, -32768);
    if(d_parms->chanscale[chn] > 2)
    {
      edf_set_physical_maximum(hdl, j, d_parms->yinc[chn] * 32767.0);
      edf_set_physical_minimum(hdl, j, d_parms->yinc[chn] * -32768.0);
      edf_set_physical_dimension(hdl, j, "V");
    }
    else
    {
      edf_set_physical_maximum(hdl, j, 1000.0 * d_parms->yinc[chn] * 32767.0);
      edf_set_physical_minimum(hdl, j, 1000.0 * d_parms->yinc[chn] * -32768.0);
      edf_set_physical_dimension(hdl, j, "mV");
    }
    snprintf(str, 512, "CHAN%i", chn + 1);
    edf_set_label(hdl, j, str);

    j++;
  }

  edf_set_equipment(hdl, d_parms->modelname);

//  printf("datrecs: %i    smps_per_record: %i\n", datrecs, smps_per_record);

  sav_data_thrd.init_save_memory_edf_file(d_parms, hdl, datrecs, smps_per_record, d_parms->wavebuf);

  wi_msg_box.setIcon(QMessageBox::NoIcon);
  wi_msg_box.setText("Saving EDF file ...");
  wi_msg_box.setStandardButtons(QMessageBox::Abort);

  connect(&sav_data_thrd, SIGNAL(finished()), &wi_msg_box, SLOT(accept()));

  sav_data_thrd.start();

  ret_stat = wi_msg_box.exec();

  if(ret_stat != QDialog::Accepted)
  {
    strlcpy(str, "Saving EDF file aborted.", 512);
    goto OUT_ERROR;
  }

  if(sav_data_thrd.get_error_num())
  {
    sav_data_thrd.get_error_str(str, 512);
    goto OUT_ERROR;
  }

OUT_NORMAL:

  disconnect(&sav_data_thrd, 0, 0, 0);

  if(hdl >= 0)
  {
    edfclose_file(hdl);
  }

  if(!strcmp(opath, ""))
  {
    statusLabel->setText("Save file canceled.");
  }
  else
  {
    statusLabel->setText("Saved memory buffer to EDF file.");
  }

  return;

OUT_ERROR:

  disconnect(&sav_data_thrd, 0, 0, 0);

  statusLabel->setText("Saving file aborted.");

  if(hdl >= 0)
  {
    edfclose_file(hdl);
  }

  wi_msg_box.setIcon(QMessageBox::Critical);
  wi_msg_box.setText(str);
  wi_msg_box.setStandardButtons(QMessageBox::Ok);
  wi_msg_box.exec();
}


//     tmc_write(":WAV:PRE?");
//
//     n = tmc_read();
//
//     if(n < 0)
//     {
//       strlcpy(str, "Can not read from device.");
//       goto OUT_ERROR;
//     }

//     printf("waveform preamble: %s\n", device->buf);

//     if(parse_preamble(device->buf, device->sz, &wfp, i))
//     {
//       strlcpy(str, "Preamble parsing error.");
//       goto OUT_ERROR;
//     }

//     printf("waveform preamble:\n"
//            "format: %i\n"
//            "type: %i\n"
//            "points: %i\n"
//            "count: %i\n"
//            "xincrement: %e\n"
//            "xorigin: %e\n"
//            "xreference: %e\n"
//            "yincrement: %e\n"
//            "yorigin: %e\n"
//            "yreference: %e\n",
//            wfp.format, wfp.type, wfp.points, wfp.count,
//            wfp.xincrement[i], wfp.xorigin[i], wfp.xreference[i],
//            wfp.yincrement[i], wfp.yorigin[i], wfp.yreference[i]);

//     rec_len = wfp.xincrement[i] * wfp.points;





void UI_Mainwindow::save_screen_waveform()
{
  int i, j,
      n=0,
      chn,
      chns=0,
      hdl=-1,
      yref[MAX_CHNS];

  char str[512],
       opath[MAX_PATHLEN];

  short *wavbuf[MAX_CHNS];

  long long rec_len=0LL;

  if(device == NULL)
  {
    return;
  }

  for(i=0; i<MAX_CHNS; i++)
  {
    wavbuf[i] = NULL;
  }

  save_data_thread get_data_thrd(0);

  QMessageBox w_msg_box;
  w_msg_box.setIcon(QMessageBox::NoIcon);
  w_msg_box.setText("Downloading data...");
  w_msg_box.setStandardButtons(QMessageBox::Abort);

  scrn_timer->stop();

  scrn_thread->wait();

  if(devparms.timebasedelayenable)
  {
    rec_len = EDFLIB_TIME_DIMENSION * devparms.timebasedelayscale * devparms.hordivisions;
  }
  else
  {
    rec_len = EDFLIB_TIME_DIMENSION * devparms.timebasescale * devparms.hordivisions;
  }

  if(rec_len < 10LL)
  {
    strlcpy(str, "Can not save waveforms when timebase < 1uSec.", 512);
    goto OUT_ERROR;
  }

  for(chn=0; chn<MAX_CHNS; chn++)
  {
    if(!devparms.chandisplay[chn])  // Download data only when channel is switched on
    {
      continue;
    }

    wavbuf[chn] = (short *)malloc(WAVFRM_MAX_BUFSZ * sizeof(short));
    if(wavbuf[chn] == NULL)
    {
      strlcpy(str, "Malloc error.", 512);
      goto OUT_ERROR;
    }

    chns++;
  }

  if(!chns)
  {
    strlcpy(str, "No active channels.", 512);
    goto OUT_ERROR;
  }

  for(chn=0; chn<MAX_CHNS; chn++)
  {
    if(!devparms.chandisplay[chn])  // Download data only when channel is switched on
    {
      continue;
    }

    usleep(20000);

    snprintf(str, 512, ":WAV:SOUR CHAN%i", chn + 1);

    tmc_write(str);

    usleep(20000);

    tmc_write(":WAV:FORM BYTE");

    usleep(20000);

    tmc_write(":WAV:MODE NORM");

    usleep(20000);

    tmc_write(":WAV:YINC?");

    usleep(20000);

    tmc_read();

    devparms.yinc[chn] = atof(device->buf);

    if(devparms.yinc[chn] < 1e-6)
    {
      snprintf(str, 512, "Error, parameter \"YINC\" out of range for channel %i: %e  line %i file %s", chn, devparms.yinc[chn], __LINE__, __FILE__);
      goto OUT_ERROR;
    }

    usleep(20000);

    tmc_write(":WAV:YREF?");

    usleep(20000);

    tmc_read();

    yref[chn] = atoi(device->buf);

    if((yref[chn] < 1) || (yref[chn] > 255))
    {
      snprintf(str, 512, "Error, parameter \"YREF\" out of range for channel %i: %i  line %i file %s", chn, yref[chn], __LINE__, __FILE__);
      goto OUT_ERROR;
    }

    usleep(20000);

    tmc_write(":WAV:YOR?");

    usleep(20000);

    tmc_read();

    devparms.yor[chn] = atoi(device->buf);

    if((devparms.yor[chn] < -255) || (devparms.yor[chn] > 255))
    {
      snprintf(str, 512, "Error, parameter \"YOR\" out of range for channel %i: %i  line %i file %s", chn, devparms.yor[chn], __LINE__, __FILE__);
      goto OUT_ERROR;
    }

    usleep(20000);

    tmc_write(":WAV:DATA?");

    connect(&get_data_thrd, SIGNAL(finished()), &w_msg_box, SLOT(accept()));

    get_data_thrd.start();

    if(w_msg_box.exec() != QDialog::Accepted)
    {
      disconnect(&get_data_thrd, 0, 0, 0);
      strlcpy(str, "Aborted by user.", 512);
      goto OUT_ERROR;
    }

    disconnect(&get_data_thrd, 0, 0, 0);

    n = get_data_thrd.get_num_bytes_rcvd();
    if(n < 0)
    {
      strlcpy(str, "Can not read from device.", 512);
      goto OUT_ERROR;
    }

    if(n > WAVFRM_MAX_BUFSZ)
    {
      snprintf(str, 512, "Datablock too big for buffer: %i", n);
      goto OUT_ERROR;
    }

    if(n < 16)
    {
      strlcpy(str, "Not enough data in buffer.", 512);
      goto OUT_ERROR;
    }

    for(i=0; i<n; i++)
    {
      wavbuf[chn][i] = ((int)(((unsigned char *)device->buf)[i]) - yref[chn] - devparms.yor[chn]) << 5;
    }
  }

  opath[0] = 0;
  if(recent_savedir[0]!=0)
  {
    strlcpy(opath, recent_savedir, MAX_PATHLEN);
    strlcat(opath, "/", MAX_PATHLEN);
  }
  strlcat(opath, "waveform.edf", MAX_PATHLEN);

  strlcpy(opath, QFileDialog::getSaveFileName(this, "Save file", opath, "EDF files (*.edf *.EDF)").toLocal8Bit().data(), MAX_PATHLEN);

  if(!strcmp(opath, ""))
  {
    goto OUT_NORMAL;
  }

  get_directory_from_path(recent_savedir, opath, MAX_PATHLEN);

  hdl = edfopen_file_writeonly(opath, EDFLIB_FILETYPE_EDFPLUS, chns);
  if(hdl < 0)
  {
    strlcpy(str, "Can not create EDF file.", 512);
    goto OUT_ERROR;
  }

  if(edf_set_datarecord_duration(hdl, rec_len / 100LL))
  {
    snprintf(str, 512, "Can not set datarecord duration of EDF file: %lli", rec_len / 100LL);
    goto OUT_ERROR;
  }

  j = 0;

  for(chn=0; chn<MAX_CHNS; chn++)
  {
    if(!devparms.chandisplay[chn])
    {
      continue;
    }

    edf_set_samplefrequency(hdl, j, n);
    edf_set_digital_maximum(hdl, j, 32767);
    edf_set_digital_minimum(hdl, j, -32768);
    if(devparms.chanscale[chn] > 2)
    {
      edf_set_physical_maximum(hdl, j, devparms.yinc[chn] * 32767.0 / 32.0);
      edf_set_physical_minimum(hdl, j, devparms.yinc[chn] * -32768.0 / 32.0);
      edf_set_physical_dimension(hdl, j, "V");
    }
    else
    {
      edf_set_physical_maximum(hdl, j, 1000.0 * devparms.yinc[chn] * 32767.0 / 32.0);
      edf_set_physical_minimum(hdl, j, 1000.0 * devparms.yinc[chn] * -32768.0 / 32.0);
      edf_set_physical_dimension(hdl, j, "mV");
    }
    snprintf(str, 512, "CHAN%i", chn + 1);
    edf_set_label(hdl, j, str);

    j++;
  }

  edf_set_equipment(hdl, devparms.modelname);

  for(chn=0; chn<MAX_CHNS; chn++)
  {
    if(!devparms.chandisplay[chn])
    {
      continue;
    }

    if(edfwrite_digital_short_samples(hdl, wavbuf[chn]))
    {
      strlcpy(str, "A write error occurred.", 512);
      goto OUT_ERROR;
    }
  }

OUT_NORMAL:

  if(hdl >= 0)
  {
    edfclose_file(hdl);
  }

  for(chn=0; chn<MAX_CHNS; chn++)
  {
    free(wavbuf[chn]);
    wavbuf[chn] = NULL;
  }

  scrn_timer->start(devparms.screentimerival);

  return;

OUT_ERROR:

  if(get_data_thrd.isFinished() != true)
  {
    connect(&get_data_thrd, SIGNAL(finished()), &w_msg_box, SLOT(accept()));
    w_msg_box.setText("Waiting for thread to finish, please wait...");
    w_msg_box.exec();
  }

  QMessageBox msgBox;
  msgBox.setIcon(QMessageBox::Critical);
  msgBox.setText(str);
  msgBox.exec();

  if(hdl >= 0)
  {
    edfclose_file(hdl);
  }

  for(chn=0; chn<MAX_CHNS; chn++)
  {
    free(wavbuf[chn]);
    wavbuf[chn] = NULL;
  }

  scrn_timer->start(devparms.screentimerival);
}

















