/*
***************************************************************************
*
* Author: Teunis van Beelen
*
* Copyright (C) 2010 - 2023 Teunis van Beelen
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



#include "signalcurve.h"

#include "time.h"



SignalCurve::SignalCurve(QWidget *w_parent) : QWidget(w_parent)
{
  int i;

  mainwindow = (UI_Mainwindow *)w_parent;

  setAttribute(Qt::WA_OpaquePaintEvent);

  SignalColor[0] = Qt::blue;
  SignalColor[1] = Qt::blue;
  SignalColor[2] = Qt::blue;
  SignalColor[3] = Qt::blue;
  tracewidth = 0;
  BackgroundColor = Qt::gray;
  RasterColor = Qt::darkGray;
  TextColor = Qt::black;

  smallfont.setFamily("Arial");
  smallfont.setPixelSize(8);

  trig_line_timer = new QTimer(this);
  trig_line_timer->setSingleShot(true);

  trig_stat_timer = new QTimer(this);

#if QT_VERSION >= 0x050000
  trig_line_timer->setTimerType(Qt::PreciseTimer);
  trig_stat_timer->setTimerType(Qt::PreciseTimer);
#endif

  bufsize = 0;
  bordersize = 60;

  v_sense = 1;

  mouse_x = 0;
  mouse_y = 0;
  mouse_old_x = 0;
  mouse_old_y = 0;

  label_active = LABEL_ACTIVE_NONE;

  for(i=0; i<MAX_CHNS; i++)
  {
    chan_arrow_moving[i] = 0;

    chan_arrow_pos[i] = 127;

    chan_tmp_y_pixel_offset[i] = 0;

    chan_tmp_old_y_pixel_offset[i] = 0;
  }

  trig_level_arrow_moving = 0;

  trig_pos_arrow_moving = 0;

  fft_arrow_pos = 0;

  fft_arrow_moving = 0;

  trig_line_visible = 0;

  trig_stat_flash = 0;

  trig_level_arrow_pos = 127;

  trig_pos_arrow_pos = 100;

  use_move_events = 0;

  updates_enabled = true;

  old_w = 10000;

  devparms = NULL;

  device = NULL;

  connect(trig_line_timer, SIGNAL(timeout()), this, SLOT(trig_line_timer_handler()));
  connect(trig_stat_timer, SIGNAL(timeout()), this, SLOT(trig_stat_timer_handler()));
}


void SignalCurve::clear()
{
  bufsize = 0;

  update();
}


void SignalCurve::resizeEvent(QResizeEvent *resize_event)
{
  QWidget::resizeEvent(resize_event);
}


void SignalCurve::setUpdatesEnabled(bool enabled)
{
  updates_enabled = enabled;
}


void SignalCurve::paintEvent(QPaintEvent *)
{
  if(updates_enabled == false)  return;

  QPainter paint(this);
#if (QT_VERSION >= 0x050000) && (QT_VERSION < 0x060000)
  paint.setRenderHint(QPainter::Qt4CompatiblePainting, true);
#endif

  smallfont.setPixelSize(devparms->font_size);

  paint.setFont(smallfont);

  drawWidget(&paint, width(), height());

  old_w = width();
}


int SignalCurve::print_to_image(const char *path)
{
  int w_p, h_p;

  if(updates_enabled == false)  return -1;

  if(path == NULL)  return -2;

  if(strlen(path) < 1)  return -3;

  w_p = width();
  h_p = height();

  QPixmap pixmap(w_p, h_p);

  QPainter paint(&pixmap);
#if (QT_VERSION >= 0x050000) && (QT_VERSION < 0x060000)
  paint.setRenderHint(QPainter::Qt4CompatiblePainting, true);
#endif

  smallfont.setPixelSize(devparms->font_size);

  paint.setFont(smallfont);

  drawWidget(&paint, w_p, h_p);

  pixmap.save(path, "PNG", 90);

  return 0;
}


void SignalCurve::drawWidget(QPainter *painter, int curve_w, int curve_h)
{
  int i, chn, tmp, rot=1, small_rulers, curve_w_backup, curve_h_backup, w_trace_offset,
      chns_done;

  char str[1024];

  double h_step=0.0,
         step,
         step2;

//  clk_start = clock();

  if(devparms == NULL)
  {
    return;
  }

  paint_mutex.lock();

  curve_w_backup = curve_w;

  curve_h_backup = curve_h;

  small_rulers = 5 * devparms->hordivisions;

  painter->fillRect(0, 0, curve_w, curve_h, BackgroundColor);

  if((curve_w < ((bordersize * 2) + 5)) || (curve_h < ((bordersize * 2) + 5)))
  {
    paint_mutex.unlock();
    return;
  }

  painter->fillRect(0, 0, curve_w, 30, QColor(32, 32, 32));

  drawTopLabels(painter);

  if((devparms->acquirememdepth > 1000) && !devparms->timebasedelayenable)
  {
    tmp = 405 - ((devparms->timebaseoffset / (devparms->acquirememdepth / devparms->samplerate)) * 233);
  }
  else
  {
    tmp = 405 - ((devparms->timebaseoffset / ((double)devparms->timebasescale * (double)devparms->hordivisions)) * 233);
  }

  if(tmp < 289)
  {
    tmp = 284;

    rot = 2;
  }
  else if(tmp > 521)
    {
      tmp = 526;

      rot = 0;
    }

  if((rot == 0) || (rot == 2))
  {
    drawSmallTriggerArrow(painter, tmp, 11, rot, QColor(255, 128, 0));
  }
  else
  {
    drawSmallTriggerArrow(painter, tmp, 16, rot, QColor(255, 128, 0));
  }

  painter->fillRect(0, curve_h - 30, curve_w, curve_h, QColor(32, 32, 32));

  for(i=0; i<devparms->channel_cnt; i++)
  {
    drawChanLabel(painter, 8 + (i * 130), curve_h - 25, i);
  }

  if(devparms->connected && devparms->show_fps)
  {
    drawfpsLabel(painter, curve_w - 80, curve_h - 11);
  }

/////////////////////////////////// translate coordinates, draw and fill a rectangle ///////////////////////////////////////////

  painter->translate(bordersize, bordersize);

  curve_w -= (bordersize * 2);

  curve_h -= (bordersize * 2);

  if(devparms->math_fft && devparms->math_fft_split)
  {
    curve_h /= 3;
  }

/////////////////////////////////// draw the rasters ///////////////////////////////////////////

  painter->setPen(RasterColor);

  painter->drawRect (0, 0, curve_w - 1, curve_h - 1);

  if((devparms->math_fft == 0) || (devparms->math_fft_split == 0))
  {
    if(devparms->displaygrid)
    {
      painter->setPen(QPen(QBrush(RasterColor, Qt::SolidPattern), tracewidth, Qt::DotLine, Qt::SquareCap, Qt::BevelJoin));

      if(devparms->displaygrid == 2)
      {
        step = (double)curve_w / (double)devparms->hordivisions;

        for(i=1; i<devparms->hordivisions; i++)
        {
          painter->drawLine(step * i, curve_h - 1, step * i, 0);
        }

        step = curve_h / (double)devparms->vertdivisions;

        for(i=1; i<devparms->vertdivisions; i++)
        {
          painter->drawLine(0, step * i, curve_w - 1, step * i);
        }
      }
      else
      {
        painter->drawLine(curve_w / 2, curve_h - 1, curve_w / 2, 0);

        painter->drawLine(0, curve_h / 2, curve_w - 1, curve_h / 2);
      }
    }

    painter->setPen(RasterColor);

    step = (double)curve_w / (double)small_rulers;

    for(i=1; i<small_rulers; i++)
    {
      step2 = step * i;

      if(devparms->displaygrid)
      {
        painter->drawLine(step2, curve_h / 2 + 2, step2, curve_h / 2 - 2);
      }

      if(i % 5)
      {
        painter->drawLine(step2, curve_h - 1, step2, curve_h - 5);

        painter->drawLine(step2, 0, step2, 4);
      }
      else
      {
        painter->drawLine(step2, curve_h - 1, step2, curve_h - 9);

        painter->drawLine(step2, 0, step2, 8);
      }
    }

    step = curve_h / (5.0 * devparms->vertdivisions);

    for(i=1; i<(5 * devparms->vertdivisions); i++)
    {
      step2 = step * i;

      if(devparms->displaygrid)
      {
        painter->drawLine(curve_w / 2 + 2, step2, curve_w / 2  - 2, step2);
      }

      if(i % 5)
      {
        painter->drawLine(curve_w - 1, step2, curve_w - 5, step2);

        painter->drawLine(0, step2, 4, step2);
      }
      else
      {
        painter->drawLine(curve_w - 1, step2, curve_w - 9, step2);

        painter->drawLine(0, step2, 8, step2);
      }
    }
  }  // if((devparms->math_fft == 0) || (devparms->math_fft_split == 0))
  else
  {
    painter->drawLine(curve_w / 2, curve_h - 1, curve_w / 2, 0);

    painter->drawLine(0, curve_h / 2, curve_w - 1, curve_h / 2);
  }

/////////////////////////////////// draw the arrows ///////////////////////////////////////////

  if(devparms->modelserie == 6)
  {
    v_sense = -((double)curve_h / 256.0);
  }
  else if(devparms->modelserie == 4)
    {
      v_sense = -((double)curve_h / (32.0 * devparms->vertdivisions));
    }
    else
    {
      v_sense = -((double)curve_h / (25.0 * devparms->vertdivisions));
    }

  drawTrigCenterArrow(painter, curve_w / 2, 0);

  for(chn=0; chn<devparms->channel_cnt; chn++)
  {
    if(!devparms->chandisplay[chn])
    {
      continue;
    }

    if(chan_arrow_moving[chn])
    {
      drawArrow(painter, 0, chan_arrow_pos[chn], 0, SignalColor[chn], '1' + chn);
    }
    else
    {
      chan_arrow_pos[chn] =  (curve_h / 2) - (devparms->chanoffset[chn] / ((devparms->chanscale[chn] * devparms->vertdivisions) / curve_h));

      if(chan_arrow_pos[chn] < 0)
      {
        chan_arrow_pos[chn] = -1;

        drawArrow(painter, -6, chan_arrow_pos[chn], 3, SignalColor[chn], '1' + chn);
      }
      else if(chan_arrow_pos[chn] > curve_h)
        {
          chan_arrow_pos[chn] = curve_h + 1;

          drawArrow(painter, -6, chan_arrow_pos[chn], 1, SignalColor[chn], '1' + chn);
        }
        else
        {
          drawArrow(painter, 0, chan_arrow_pos[chn], 0, SignalColor[chn], '1' + chn);
        }
    }
  }

/////////////////////////////////// FFT: draw the curve ///////////////////////////////////////////

  if((devparms->math_fft == 1) && (devparms->math_fft_split == 0))
  {
    drawFFT(painter, curve_h_backup, curve_w_backup);
  }

/////////////////////////////////// draw the curve ///////////////////////////////////////////

  if(bufsize > 32)
  {
    painter->setClipping(true);
    painter->setClipRegion(QRegion(0, 0, curve_w, curve_h), Qt::ReplaceClip);

    h_step = (double)curve_w / (devparms->hordivisions * 100);

    for(chn=0, chns_done=0; chn<=devparms->channel_cnt; chn++)
    {
      if(chns_done)  break;

      if(chn == devparms->activechannel)  continue;

      if(chn == devparms->channel_cnt)
      {
        chn = devparms->activechannel;

        chns_done = 1;
      }

      if(!devparms->chandisplay[chn])
      {
        continue;
      }

      w_trace_offset = (curve_w / 2.0) - (((devparms->timebaseoffset - devparms->xorigin[chn]) / devparms->timebasescale) * ((double)curve_w / (double)(devparms->hordivisions)));

      painter->setPen(QPen(QBrush(SignalColor[chn], Qt::SolidPattern), tracewidth, Qt::SolidLine, Qt::SquareCap, Qt::BevelJoin));

      for(i=0; i<bufsize; i++)
      {
        if(bufsize < (curve_w / 2))
        {
          painter->drawLine(i * h_step + w_trace_offset,
                            (devparms->wavebuf[chn][i] * v_sense) + (curve_h / 2) - chan_tmp_y_pixel_offset[chn],
                            (i + 1) * h_step + w_trace_offset,
                            (devparms->wavebuf[chn][i] * v_sense) + (curve_h / 2) - chan_tmp_y_pixel_offset[chn]);
          if(i)
          {
            painter->drawLine(i * h_step + w_trace_offset,
                              (devparms->wavebuf[chn][i - 1] * v_sense) + (curve_h / 2) - chan_tmp_y_pixel_offset[chn],
                              i * h_step + w_trace_offset,
                              (devparms->wavebuf[chn][i] * v_sense) + (curve_h / 2) - chan_tmp_y_pixel_offset[chn]);
          }
        }
        else
        {
          if(i < (bufsize - 1))
          {
            if(devparms->displaytype)
            {
              painter->drawPoint(i * h_step + w_trace_offset,
                                 (devparms->wavebuf[chn][i] * v_sense) + (curve_h / 2) - chan_tmp_y_pixel_offset[chn]);
            }
            else
            {
              painter->drawLine(i * h_step + w_trace_offset,
                                (devparms->wavebuf[chn][i] * v_sense) + (curve_h / 2) - chan_tmp_y_pixel_offset[chn],
                                (i + 1) * h_step + w_trace_offset,
                                (devparms->wavebuf[chn][i + 1] * v_sense) + (curve_h / 2) - chan_tmp_y_pixel_offset[chn]);
            }
          }
        }
      }
    }

    painter->setClipping(false);
  }

/////////////////////////////////// draw the decoder ///////////////////////////////////////////

  if(devparms->math_decode_display)  draw_decoder(painter, curve_w, curve_h);

/////////////////////////////////// draw the trigger arrows ///////////////////////////////////////////

  if(trig_level_arrow_moving)
  {
    drawArrow(painter, curve_w, trig_level_arrow_pos, 2, QColor(255, 128, 0), 'T');

    painter->setPen(QPen(QBrush(QColor(255, 128, 0), Qt::SolidPattern), tracewidth, Qt::DashDotLine, Qt::SquareCap, Qt::BevelJoin));

    painter->drawLine(1, trig_level_arrow_pos, curve_w - 2, trig_level_arrow_pos);
  }
  else
  {
    if(devparms->triggeredgesource <= TRIG_SRC_CHAN4)
    {
      trig_level_arrow_pos = (curve_h / 2) - ((devparms->triggeredgelevel[devparms->triggeredgesource] + devparms->chanoffset[devparms->triggeredgesource]) / ((devparms->chanscale[devparms->triggeredgesource] * devparms->vertdivisions) / curve_h));

      if(trig_level_arrow_pos < 0)
      {
        trig_level_arrow_pos = -1;

        drawArrow(painter, curve_w + 6, trig_level_arrow_pos, 3, QColor(255, 128, 0), 'T');
      }
      else if(trig_level_arrow_pos > curve_h)
        {
          trig_level_arrow_pos = curve_h + 1;

          drawArrow(painter, curve_w + 6, trig_level_arrow_pos, 1, QColor(255, 128, 0), 'T');
        }
        else
        {
          drawArrow(painter, curve_w, trig_level_arrow_pos, 2, QColor(255, 128, 0), 'T');
        }
    }

    if(trig_line_visible)
    {
      painter->setPen(QPen(QBrush(QColor(255, 128, 0), Qt::SolidPattern), tracewidth, Qt::DashDotLine, Qt::SquareCap, Qt::BevelJoin));

      painter->drawLine(1, trig_level_arrow_pos, curve_w - 2, trig_level_arrow_pos);
    }
  }

  if(trig_pos_arrow_moving)
  {
    drawArrow(painter, trig_pos_arrow_pos, 27, 1, QColor(255, 128, 0), 'T');
  }
  else
  {
    if(devparms->timebasedelayenable)
    {
      trig_pos_arrow_pos = (curve_w / 2) - ((devparms->timebasedelayoffset / ((double)devparms->timebasedelayscale * (double)devparms->hordivisions)) * curve_w);
    }
    else
    {
      trig_pos_arrow_pos = (curve_w / 2) - ((devparms->timebaseoffset / ((double)devparms->timebasescale * (double)devparms->hordivisions)) * curve_w);
    }

    if(trig_pos_arrow_pos < 0)
    {
      trig_pos_arrow_pos = -1;

      drawArrow(painter, trig_pos_arrow_pos, 18, 2, QColor(255, 128, 0), 'T');
    }
    else if(trig_pos_arrow_pos > curve_w)
      {
        trig_pos_arrow_pos = curve_w + 1;

        drawArrow(painter, trig_pos_arrow_pos, 18, 0, QColor(255, 128, 0), 'T');
      }
      else
      {
        drawArrow(painter, trig_pos_arrow_pos, 27, 1, QColor(255, 128, 0), 'T');
      }
  }

  if(devparms->countersrc)
  {
    paintCounterLabel(painter, curve_w - 180, 6);
  }

  if(devparms->func_wrec_enable)
  {
    paintPlaybackLabel(painter, curve_w - 180, 40);
  }

  if((mainwindow->adjDialFunc == ADJ_DIAL_FUNC_HOLDOFF) || (mainwindow->navDialFunc == NAV_DIAL_FUNC_HOLDOFF))
  {
    convert_to_metric_suffix(str, devparms->triggerholdoff, 2, 1024);

    strlcat(str, "S", 1024);

    paintLabel(painter, curve_w - 110, 5, 100, 20, str, Qt::white);
  }
  else if(mainwindow->adjDialFunc == ADJ_DIAL_FUNC_ACQ_AVG)
    {
      snprintf(str, 1024, "%i", devparms->acquireaverages);

      paintLabel(painter, curve_w - 110, 5, 100, 20, str, Qt::white);
    }

  if(label_active == LABEL_ACTIVE_TRIG)
  {
    convert_to_metric_suffix(str, devparms->triggeredgelevel[devparms->triggeredgesource], 2, 1024);

    strlcat(str, devparms->chanunitstr[devparms->chanunit[devparms->triggeredgesource]], 1024);

    paintLabel(painter, curve_w - 120, curve_h - 50, 100, 20, str, QColor(255, 128, 0));
  }
  else if(label_active == LABEL_ACTIVE_CHAN1)
    {
      convert_to_metric_suffix(str, devparms->chanoffset[0], 2, 1024);

      strlcat(str, devparms->chanunitstr[devparms->chanunit[0]], 1024);

      paintLabel(painter, 20, curve_h - 50, 100, 20, str, SignalColor[0]);
    }
    else if(label_active == LABEL_ACTIVE_CHAN2)
      {
        convert_to_metric_suffix(str, devparms->chanoffset[1], 2, 1024);

        strlcat(str, devparms->chanunitstr[devparms->chanunit[1]], 1024);

        paintLabel(painter, 20, curve_h - 50, 100, 20, str, SignalColor[1]);
      }
      else if(label_active == LABEL_ACTIVE_CHAN3)
        {
          convert_to_metric_suffix(str, devparms->chanoffset[2], 2, 1024);

          strlcat(str, devparms->chanunitstr[devparms->chanunit[2]], 1024);

          paintLabel(painter, 20, curve_h - 50, 100, 20, str, SignalColor[2]);
        }
        else if(label_active == LABEL_ACTIVE_CHAN4)
          {
            convert_to_metric_suffix(str, devparms->chanoffset[3], 2, 1024);

            strlcat(str, devparms->chanunitstr[devparms->chanunit[3]], 1024);

            paintLabel(painter, 20, curve_h - 50, 100, 20, str, SignalColor[3]);
          }

  if((devparms->math_fft == 1) && (devparms->math_fft_split == 1))
  {
    drawFFT(painter, curve_h_backup, curve_w_backup);

    if(label_active == LABEL_ACTIVE_FFT)
    {
      if(devparms->math_fft_unit == 0)
      {
        strlcpy(str, "POS: ", 1024);

        convert_to_metric_suffix(str + strlen(str), devparms->fft_voffset, 1, 1024);

        strlcat(str, "V", 1024);
      }
      else
      {
        snprintf(str, 1024, "POS: %.1fdB", devparms->fft_voffset);
      }

      paintLabel(painter, 20, curve_h * 1.85 - 50.0, 100, 20, str, QColor(128, 64, 255));
    }
  }

//   clk_end = clock();
//
//   cpu_time_used += ((double) (clk_end - clk_start)) / CLOCKS_PER_SEC;
//
//   scr_update_cntr++;
//
//   if(!(scr_update_cntr % 50))
//   {
//     printf("CPU time used: %f\n", cpu_time_used / 50);
//
//     cpu_time_used = 0;
//   }

  paint_mutex.unlock();
}


void SignalCurve::drawFFT(QPainter *painter, int curve_h_b, int curve_w_b)
{
  int i, small_rulers, curve_w, curve_h;

  char str[1024];

  double h_step=0.0,
         step,
         step2,
         fft_h_offset=0.0;

  small_rulers = 5 * devparms->hordivisions;

/////////////////////////////////// FFT: translate coordinates, draw and fill a rectangle ///////////////////////////////////////////

  if(devparms->math_fft_split == 0)
  {
    curve_w = curve_w_b - (bordersize * 2);

    curve_h = curve_h_b - (bordersize * 2);
  }
  else
  {
    curve_h = curve_h_b - (bordersize * 2);

    curve_h /= 3;

    painter->resetTransform();

    painter->translate(bordersize, bordersize + curve_h + 15);

    curve_w = curve_w_b - (bordersize * 2);

    curve_h = curve_h_b - (bordersize * 2);

    curve_h *= 0.64;

/////////////////////////////////// FFT: draw the rasters ///////////////////////////////////////////

    painter->setPen(RasterColor);

    painter->drawRect (0, 0, curve_w - 1, curve_h - 1);

    if(devparms->displaygrid)
    {
      painter->setPen(QPen(QBrush(RasterColor, Qt::SolidPattern), tracewidth, Qt::DotLine, Qt::SquareCap, Qt::BevelJoin));

      if(devparms->displaygrid == 2)
      {
        step = (double)curve_w / (double)devparms->hordivisions;

        for(i=1; i<devparms->hordivisions; i++)
        {
          painter->drawLine(step * i, curve_h - 1, step * i, 0);
        }

        step = curve_h / 8.0;

        for(i=1; i<8; i++)
        {
          painter->drawLine(0, step * i, curve_w - 1, step * i);
        }
      }
      else
      {
        painter->drawLine(curve_w / 2, curve_h - 1, curve_w / 2, 0);

        painter->drawLine(0, curve_h / 2, curve_w - 1, curve_h / 2);
      }
    }

    painter->setPen(RasterColor);

    step = (double)curve_w / (double)small_rulers;

    for(i=1; i<small_rulers; i++)
    {
      step2 = step * i;

      if(devparms->displaygrid)
      {
        painter->drawLine(step2, curve_h / 2 + 2, step2, curve_h / 2 - 2);
      }

      if(i % 5)
      {
        painter->drawLine(step2, curve_h - 1, step2, curve_h - 5);

        painter->drawLine(step2, 0, step2, 4);
      }
      else
      {
        painter->drawLine(step2, curve_h - 1, step2, curve_h - 9);

        painter->drawLine(step2, 0, step2, 8);
      }
    }

    step = curve_h / 40.0;

    for(i=1; i<40; i++)
    {
      step2 = step * i;

      if(devparms->displaygrid)
      {
        painter->drawLine(curve_w / 2 + 2, step2, curve_w / 2  - 2, step2);
      }

      if(i % 5)
      {
        painter->drawLine(curve_w - 1, step2, curve_w - 5, step2);

        painter->drawLine(0, step2, 4, step2);
      }
      else
      {
        painter->drawLine(curve_w - 1, step2, curve_w - 9, step2);

        painter->drawLine(0, step2, 8, step2);
      }
    }

/////////////////////////////////// FFT: draw the arrow ///////////////////////////////////////////

    fft_arrow_pos = (curve_h / 2.0) - (((double)curve_h / (8.0 * devparms->fft_vscale)) * devparms->fft_voffset);

    drawArrow(painter, 0, fft_arrow_pos, 0, QColor(128, 64, 255), 'M');
  }

/////////////////////////////////// FFT: draw the curve ///////////////////////////////////////////

  if(bufsize < 32)
  {
    return;
  }

  if((devparms->fftbufsz > 32) && devparms->chandisplay[devparms->math_fft_src])
  {
    painter->setClipping(true);
    painter->setClipRegion(QRegion(0, 0, curve_w, curve_h), Qt::ReplaceClip);

    h_step = (double)curve_w / (double)devparms->fftbufsz;

    if(devparms->timebasedelayenable)
    {
      h_step *= (100.0 / devparms->timebasedelayscale) / devparms->math_fft_hscale;
    }
    else
    {
      h_step *= (100.0 / devparms->timebasescale) / devparms->math_fft_hscale;
    }

    if(devparms->modelserie != 1)
    {
      h_step /= 28.0;
    }
    else
    {
      h_step /= 24.0;
    }

    fft_v_sense = (double)curve_h / (-8.0 * devparms->fft_vscale);

    fft_v_offset = (curve_h / 2.0) + (fft_v_sense * devparms->fft_voffset);

    fft_h_offset = (curve_w / 2) - ((devparms->math_fft_hcenter / devparms->math_fft_hscale) * curve_w / devparms->hordivisions);

//     fft_smpls_onscreen = (double)devparms->fftbufsz * ((devparms->math_fft_hscale * devparms->hordivisions) / (double)devparms->current_screen_sf);

    painter->setPen(QPen(QBrush(QColor(128, 64, 255), Qt::SolidPattern), tracewidth, Qt::SolidLine, Qt::SquareCap, Qt::BevelJoin));

    for(i=0; i<(devparms->fftbufsz - 1); i++)
    {
//       if(fft_smpls_onscreen < (curve_w / 2))
//       {
//         painter->drawLine(i * h_step + fft_h_offset, (devparms->fftbuf_out[i] * fft_v_sense) + fft_v_offset, (i + 1) * h_step + fft_h_offset, (devparms->fftbuf_out[i] * fft_v_sense) + fft_v_offset);
//         if(i)
//         {
//           painter->drawLine(i * h_step + fft_h_offset, (devparms->fftbuf_out[i - 1] * fft_v_sense) + fft_v_offset, i * h_step + fft_h_offset, (devparms->fftbuf_out[i] * fft_v_sense) + fft_v_offset);
//         }
//       }
//       else
//       {
//         if(i < (devparms->fftbufsz - 1))
//         {
          painter->drawLine(i * h_step + fft_h_offset, (devparms->fftbuf_out[i] * fft_v_sense) + fft_v_offset, (i + 1) * h_step + fft_h_offset, (devparms->fftbuf_out[i + 1] * fft_v_sense) + fft_v_offset);
//          }
//       }
    }

    snprintf(str, 1024, "FFT:  CH%i  ", devparms->math_fft_src + 1);

    convert_to_metric_suffix(str + strlen(str), devparms->fft_vscale, 2, 1024 - strlen(str));

    if(devparms->math_fft_unit == 0)
    {
      strlcat(str, "V/Div   Center ", 1024);
    }
    else
    {
      strlcat(str, "dBV/Div   Center ", 1024);
    }

    convert_to_metric_suffix(str + strlen(str), devparms->math_fft_hcenter, 1, 1024 - strlen(str));

    strlcat(str, "Hz   ", 1024);

    convert_to_metric_suffix(str + strlen(str), devparms->math_fft_hscale, 2, 1024 - strlen(str));

    strlcat(str, "Hz/Div   ", 1024);

    if(devparms->timebasedelayenable)
    {
      convert_to_metric_suffix(str + strlen(str), 100.0 / devparms->timebasedelayscale, 0, 1024 - strlen(str));
    }
    else
    {
      convert_to_metric_suffix(str + strlen(str), 100.0 / devparms->timebasescale, 0, 1024 - strlen(str));
    }

    strlcat(str, "Sa/s", 1024);

    painter->drawText(15, 30, str);

    painter->setClipping(false);
  }
  else
  {
    painter->setPen(QPen(QBrush(QColor(128, 64, 255), Qt::SolidPattern), tracewidth, Qt::SolidLine, Qt::SquareCap, Qt::BevelJoin));

    snprintf(str, 1024, "FFT: CH%i Data Invalid!", devparms->math_fft_src + 1);

    painter->drawText(15, 30, str);
  }
}


void SignalCurve::drawCurve(struct device_settings *devp, struct tmcdev *dev)
{
  devparms = devp;

  device = dev;

  bufsize = devparms->wavebufsz;

  update();
}


void SignalCurve::setDeviceParameters(struct device_settings *devp)
{
  devparms = devp;
}


void SignalCurve::drawTopLabels(QPainter *painter)
{
  int i, x1, tmp;

  char str[512];

  double dtmp1, dtmp2;

  QPainterPath path;

  if(devparms == NULL)
  {
    return;
  }

  path.addRoundedRect(5, 5, 70, 20, 3, 3);

  painter->fillPath(path, Qt::black);

  painter->setPen(Qt::white);

  painter->drawText(5, 5, 70, 20, Qt::AlignCenter, devparms->modelname);

//////////////// triggerstatus ///////////////////////////////

  path = QPainterPath();

  path.addRoundedRect(80, 5, 35, 20, 3, 3);

  if((devparms->triggerstatus == 1) || (devparms->triggerstatus == 3))
  {
    if(!trig_stat_flash)
    {
      trig_stat_flash = 1;

      trig_stat_timer->start(1000);
    }
  }
  else
  {
    if(trig_stat_flash)
    {
      trig_stat_flash = 0;

      trig_stat_timer->stop();
    }
  }

  if(trig_stat_flash == 2)
  {
    painter->fillPath(path, Qt::green);

    painter->setPen(Qt::black);
  }
  else
  {
    painter->fillPath(path, Qt::black);

    painter->setPen(Qt::green);
  }

  switch(devparms->triggerstatus)
  {
    case 0 : painter->drawText(80, 5, 35, 20, Qt::AlignCenter, "T'D");
             break;
    case 1 : painter->drawText(80, 5, 35, 20, Qt::AlignCenter, "WAIT");
             break;
    case 2 : painter->drawText(80, 5, 35, 20, Qt::AlignCenter, "RUN");
             break;
    case 3 : painter->drawText(80, 5, 35, 20, Qt::AlignCenter, "AUTO");
             break;
    case 4 : painter->drawText(80, 5, 35, 20, Qt::AlignCenter, "FIN");
             break;
    case 5 : painter->setPen(Qt::red);
             painter->drawText(80, 5, 35, 20, Qt::AlignCenter, "STOP");
             break;
  }

//////////////// horizontal ///////////////////////////////

  path = QPainterPath();

  path.addRoundedRect(140, 5, 70, 20, 3, 3);

  painter->fillPath(path, Qt::black);

  painter->setPen(Qt::white);

  painter->drawText(125, 20, "H");

  if(devparms->timebasedelayenable)
  {
    convert_to_metric_suffix(str, devparms->timebasedelayscale, 1, 512);
  }
  else
  {
    convert_to_metric_suffix(str, devparms->timebasescale, 1, 512);
  }

  remove_trailing_zeros(str);

  strlcat(str, "s", 512);

  painter->drawText(140, 5, 70, 20, Qt::AlignCenter, str);

//////////////// samplerate ///////////////////////////////

  painter->setPen(Qt::gray);

  convert_to_metric_suffix(str, devparms->samplerate, 0, 512);

  strlcat(str, "Sa/s", 512);

  painter->drawText(200, -1, 85, 20, Qt::AlignCenter, str);

  if(devparms->acquirememdepth)
  {
    convert_to_metric_suffix(str, devparms->acquirememdepth, 1, 512);

    remove_trailing_zeros(str);

    strlcat(str, "pts", 512);

    painter->drawText(200, 14, 85, 20, Qt::AlignCenter, str);
  }
  else
  {
    painter->drawText(200, 14, 85, 20, Qt::AlignCenter, "AUTO");
  }

//////////////// memory position ///////////////////////////////

  path = QPainterPath();

  path.addRoundedRect(285, 5, 240, 20, 3, 3);

  painter->fillPath(path, Qt::black);

  painter->setPen(Qt::gray);

  if(devparms->timebasedelayenable)
  {
    dtmp1 = devparms->timebasedelayscale / devparms->timebasescale;

    dtmp2 = (devparms->timebaseoffset - devparms->timebasedelayoffset) / ((devparms->hordivisions / 2) * devparms->timebasescale);

    tmp = (116 - (dtmp1 * 116)) - (dtmp2 * 116);

    if(tmp > 0)
    {
      painter->fillRect(288, 16, tmp, 8, QColor(64, 160, 255));
    }

    x1 = (116 - (dtmp1 * 116)) + (dtmp2 * 116);

    if(x1 > 0)
    {
      painter->fillRect(288 + 233 - x1, 16, x1, 8, QColor(64, 160, 255));
    }
  }
  else if(devparms->acquirememdepth > 1000)
  {
    dtmp1 = (devparms->hordivisions * devparms->timebasescale) / (devparms->acquirememdepth / devparms->samplerate);

    dtmp2 = devparms->timebaseoffset / dtmp1;

    tmp = (116 - (dtmp1 * 116)) - (dtmp2 * 116);

    if(tmp > 0)
    {
      painter->fillRect(288, 16, tmp, 8, QColor(64, 160, 255));
    }

    x1 = (116 - (dtmp1 * 116)) + (dtmp2 * 116);

    if(x1 > 0)
    {
      painter->fillRect(288 + 233 - x1, 16, x1, 8, QColor(64, 160, 255));
    }
  }

  painter->drawRect(288, 16, 233, 8);

  painter->setPen(Qt::white);

  painter->drawLine(289, 20, 291, 22);

  for(i=0; i<19; i++)
  {
    painter->drawLine((i * 12) + 291, 22, (i * 12) + 293, 22);

    painter->drawLine((i * 12) + 297, 18, (i * 12) + 299, 18);

    painter->drawLine((i * 12) + 294, 21, (i * 12) + 296, 19);

    painter->drawLine((i * 12) + 300, 19, (i * 12) + 302, 21);
  }

  painter->drawLine(519, 22, 520, 22);

//////////////// delay ///////////////////////////////

  path = QPainterPath();

  path.addRoundedRect(570, 5, 85, 20, 3, 3);

  painter->fillPath(path, Qt::black);

  painter->setPen(QColor(255, 128, 0));

  painter->drawText(555, 20, "D");

  if(devparms->timebasedelayenable)
  {
    convert_to_metric_suffix(str, devparms->timebasedelayoffset, 4, 512);
  }
  else
  {
    convert_to_metric_suffix(str, devparms->timebaseoffset, 4, 512);
  }

  strlcat(str, "s", 512);

  painter->drawText(570, 5, 85, 20, Qt::AlignCenter, str);

//////////////// trigger ///////////////////////////////

  path = QPainterPath();

  path.addRoundedRect(685, 5, 125, 20, 3, 3);

  painter->fillPath(path, Qt::black);

  painter->setPen(Qt::gray);

  painter->drawText(670, 20, "T");

  if(devparms->triggeredgesource <= TRIG_SRC_CHAN4)
  {
    convert_to_metric_suffix(str, devparms->triggeredgelevel[devparms->triggeredgesource], 2, 512);

    strlcat(str, devparms->chanunitstr[devparms->chanunit[devparms->triggeredgesource]], 512);

    painter->setPen(SignalColor[devparms->triggeredgesource]);

    painter->drawText(735, 5, 85, 20, Qt::AlignCenter, str);
  }
  else
  {
    switch(devparms->triggeredgesource)
    {
      case TRIG_SRC_EXT:
      case TRIG_SRC_EXT5: painter->setPen(Qt::green);
                          break;
      case TRIG_SRC_ACL:  painter->setPen(QColor(255, 64, 0));
                          break;
      default:            painter->setPen(Qt::white);
                          break;
    }
  }

  path = QPainterPath();

  path.addRoundedRect(725, 7, 15, 15, 3, 3);

  if(devparms->triggeredgesource <= TRIG_SRC_CHAN4)
  {
    painter->fillPath(path, SignalColor[devparms->triggeredgesource]);

    snprintf(str, 512, "%i", devparms->triggeredgesource + 1);
  }
  else
  {
    switch(devparms->triggeredgesource)
    {
      case TRIG_SRC_EXT:
      case TRIG_SRC_EXT5: painter->fillPath(path, Qt::green);
                          strlcpy(str, "E", 512);
                          break;
      case TRIG_SRC_ACL:  painter->fillPath(path, QColor(255, 64, 0));
                          strlcpy(str, "AC", 512);
                          break;
      default:            painter->fillPath(path, Qt::white);
                          snprintf(str, 512, "D%i", devparms->triggeredgesource - TRIG_SRC_LA_D0);
                          break;
    }
  }

  if(devparms->triggeredgeslope == 0)
  {
    painter->drawLine(705, 8, 710, 8);
    painter->drawLine(705, 8, 705, 20);
    painter->drawLine(700, 20, 705, 20);
    painter->drawLine(701, 15, 705, 11);
    painter->drawLine(709, 15, 705, 11);
  }

  if(devparms->triggeredgeslope == 1)
  {
    painter->drawLine(700, 8, 705, 8);
    painter->drawLine(705, 8, 705, 20);
    painter->drawLine(705, 20, 710, 20);
    painter->drawLine(701, 12, 705, 16);
    painter->drawLine(709, 12, 705, 16);
  }

  if(devparms->triggeredgeslope == 2)
  {
    painter->drawLine(702, 8, 702, 18);
    painter->drawLine(700, 10, 702, 8);
    painter->drawLine(704, 10, 702, 8);
    painter->drawLine(708, 8, 708, 18);
    painter->drawLine(706, 16, 708, 18);
    painter->drawLine(710, 16, 708, 18);
  }

  painter->setPen(Qt::black);

  painter->drawText(725, 8, 15, 15, Qt::AlignCenter, str);
}


void SignalCurve::drawfpsLabel(QPainter *painter, int xpos, int ypos)
{
  char str[512];

  static struct timespec tp1, tp2;

  painter->setPen(Qt::red);

  clock_gettime(CLOCK_REALTIME, &tp1);

  if(tp1.tv_nsec >= tp2.tv_nsec)
  {
    snprintf(str, 512, "%04.1f fps", 1.0 / ((tp1.tv_sec - tp2.tv_sec) + ((tp1.tv_nsec - tp2.tv_nsec) / 1e9)));
  }
  else
  {
    snprintf(str, 512, "%04.1f fps", 1.0 / ((tp1.tv_sec - tp2.tv_sec - 1) + ((tp1.tv_nsec - tp2.tv_nsec + 1000000000) / 1e9)));
  }

  painter->drawText(xpos, ypos, str);

  tp2 = tp1;
}


void SignalCurve::drawChanLabel(QPainter *painter, int xpos, int ypos, int chn)
{
  QPainterPath path;

  char str1[4],
       str2[512];

  if(devparms == NULL)
  {
    return;
  }

  str1[0] = '1' + chn;
  str1[1] = 0;

  convert_to_metric_suffix(str2, devparms->chanscale[chn], 2, 512);

  strlcat(str2, devparms->chanunitstr[devparms->chanunit[chn]], 512);

  if(devparms->chanbwlimit[chn])
  {
    strlcat(str2, " B", 512);
  }

  if(devparms->chandisplay[chn])
  {
    if(chn == devparms->activechannel)
    {
      path.addRoundedRect(xpos, ypos, 20, 20, 3, 3);

      painter->fillPath(path, SignalColor[chn]);

      painter->setPen(Qt::black);

      painter->drawText(xpos + 6, ypos + 15, str1);

      if(devparms->chaninvert[chn])
      {
        painter->drawLine(xpos + 6, ypos + 3, xpos + 14, ypos + 3);
      }

      path = QPainterPath();

      path.addRoundedRect(xpos + 25, ypos, 90, 20, 3, 3);

      painter->fillPath(path, Qt::black);

      painter->setPen(SignalColor[chn]);

      painter->drawRoundedRect(xpos + 25, ypos, 90, 20, 3, 3);

      painter->drawText(xpos + 35, ypos + 1, 90, 20, Qt::AlignCenter, str2);

      if(devparms->chancoupling[chn] == 0)
      {
        painter->drawLine(xpos + 33, ypos + 6, xpos + 33, ypos + 10);

        painter->drawLine(xpos + 28, ypos + 10, xpos + 38, ypos + 10);

        painter->drawLine(xpos + 30, ypos + 12, xpos + 36, ypos + 12);

        painter->drawLine(xpos + 32, ypos + 14, xpos + 34, ypos + 14);
      }
      else if(devparms->chancoupling[chn] == 1)
        {
          painter->drawLine(xpos + 28, ypos + 8, xpos + 38, ypos + 8);

          painter->drawLine(xpos + 28, ypos + 12, xpos + 30, ypos + 12);

          painter->drawLine(xpos + 32, ypos + 12, xpos + 34, ypos + 12);

          painter->drawLine(xpos + 36, ypos + 12, xpos + 38, ypos + 12);
        }
        else if(devparms->chancoupling[chn] == 2)
          {
            painter->drawArc(xpos + 30, ypos + 8, 5, 5, 10 * 16, 160 * 16);

            painter->drawArc(xpos + 35, ypos + 8, 5, 5, -10 * 16, -160 * 16);
          }
    }
    else
    {
      path.addRoundedRect(xpos, ypos, 20, 20, 3, 3);

      path.addRoundedRect(xpos + 25, ypos, 85, 20, 3, 3);

      painter->fillPath(path, Qt::black);

      painter->setPen(SignalColor[chn]);

      painter->drawText(xpos + 6, ypos + 15, str1);

      if(devparms->chaninvert[chn])
      {
        painter->drawLine(xpos + 6, ypos + 3, xpos + 14, ypos + 3);
      }

      painter->drawText(xpos + 35, ypos + 1, 90, 20, Qt::AlignCenter, str2);

      if(devparms->chancoupling[chn] == 0)
      {
        painter->drawLine(xpos + 33, ypos + 6, xpos + 33, ypos + 10);

        painter->drawLine(xpos + 28, ypos + 10, xpos + 38, ypos + 10);

        painter->drawLine(xpos + 30, ypos + 12, xpos + 36, ypos + 12);

        painter->drawLine(xpos + 32, ypos + 14, xpos + 34, ypos + 14);
      }
      else if(devparms->chancoupling[chn] == 1)
        {
          painter->drawLine(xpos + 28, ypos + 8, xpos + 38, ypos + 8);

          painter->drawLine(xpos + 28, ypos + 12, xpos + 30, ypos + 12);

          painter->drawLine(xpos + 32, ypos + 12, xpos + 34, ypos + 12);

          painter->drawLine(xpos + 36, ypos + 12, xpos + 38, ypos + 12);
        }
        else if(devparms->chancoupling[chn] == 2)
          {
            painter->drawArc(xpos + 30, ypos + 8, 5, 5, 10 * 16, 160 * 16);

            painter->drawArc(xpos + 35, ypos + 8, 5, 5, -10 * 16, -160 * 16);
          }
    }
  }
  else
  {
    path.addRoundedRect(xpos, ypos, 20, 20, 3, 3);

    path.addRoundedRect(xpos + 25, ypos, 85, 20, 3, 3);

    painter->fillPath(path, Qt::black);

    painter->setPen(QColor(48, 48, 48));

    painter->drawText(xpos + 6, ypos + 15, str1);

    painter->drawText(xpos + 30, ypos + 1, 85, 20, Qt::AlignCenter, str2);

    if(devparms->chanbwlimit[chn])
    {
      painter->drawText(xpos + 90, ypos + 1, 20, 20, Qt::AlignCenter, "B");
    }

    if(devparms->chancoupling[chn] == 0)
    {
      painter->drawLine(xpos + 33, ypos + 6, xpos + 33, ypos + 10);

      painter->drawLine(xpos + 28, ypos + 10, xpos + 38, ypos + 10);

      painter->drawLine(xpos + 30, ypos + 12, xpos + 36, ypos + 12);

      painter->drawLine(xpos + 32, ypos + 14, xpos + 34, ypos + 14);
    }
    else if(devparms->chancoupling[chn] == 1)
      {
        painter->drawLine(xpos + 28, ypos + 8, xpos + 38, ypos + 8);

        painter->drawLine(xpos + 28, ypos + 12, xpos + 30, ypos + 12);

        painter->drawLine(xpos + 32, ypos + 12, xpos + 34, ypos + 12);

        painter->drawLine(xpos + 36, ypos + 12, xpos + 38, ypos + 12);
      }
      else if(devparms->chancoupling[chn] == 2)
        {
          painter->drawArc(xpos + 30, ypos + 8, 5, 5, 10 * 16, 160 * 16);

          painter->drawArc(xpos + 35, ypos + 8, 5, 5, -10 * 16, -160 * 16);
        }
  }
}


void SignalCurve::drawArrow(QPainter *painter, int xpos, int ypos, int rot, QColor color, char ch)
{
  QPainterPath path;

  char str[4];

  str[0] = ch;
  str[1] = 0;

  if(rot == 0)
  {
    path.moveTo(xpos - 20, ypos + 6);
    path.lineTo(xpos - 7,  ypos + 6);
    path.lineTo(xpos,     ypos);
    path.lineTo(xpos - 7,  ypos - 6);
    path.lineTo(xpos - 20, ypos - 6);
    path.lineTo(xpos - 20, ypos + 6);
    painter->fillPath(path, color);

    painter->setPen(Qt::black);

    painter->drawText(xpos - 17, ypos + 4, str);
  }
  else if(rot == 1)
    {
      path.moveTo(xpos + 6, ypos - 20);
      path.lineTo(xpos + 6,  ypos - 7);
      path.lineTo(xpos,     ypos);
      path.lineTo(xpos - 6,  ypos - 7);
      path.lineTo(xpos - 6, ypos - 20);
      path.lineTo(xpos + 6, ypos - 20);
      painter->fillPath(path, color);

      painter->setPen(Qt::black);

      painter->drawText(xpos - 3, ypos - 7, str);
    }
    else if(rot == 2)
      {
        path.moveTo(xpos + 20, ypos + 6);
        path.lineTo(xpos + 7,  ypos + 6);
        path.lineTo(xpos,     ypos);
        path.lineTo(xpos + 7,  ypos - 6);
        path.lineTo(xpos + 20, ypos - 6);
        path.lineTo(xpos + 20, ypos + 6);
        painter->fillPath(path, color);

        painter->setPen(Qt::black);

        painter->drawText(xpos + 9, ypos + 4, str);
      }
      else if(rot == 3)
        {
          path.moveTo(xpos + 6, ypos + 20);
          path.lineTo(xpos + 6,  ypos + 7);
          path.lineTo(xpos,     ypos);
          path.lineTo(xpos - 6,  ypos + 7);
          path.lineTo(xpos - 6, ypos + 20);
          path.lineTo(xpos + 6, ypos + 20);
          painter->fillPath(path, color);

          painter->setPen(Qt::black);

          painter->drawText(xpos - 3, ypos + 16, str);
        }
}


void SignalCurve::drawSmallTriggerArrow(QPainter *painter, int xpos, int ypos, int rot, QColor color)
{
  QPainterPath path;

  if(rot == 0)
  {
    path.moveTo(xpos - 13, ypos - 5);
    path.lineTo(xpos - 5,  ypos - 5);
    path.lineTo(xpos,     ypos);
    path.lineTo(xpos - 5,  ypos + 5);
    path.lineTo(xpos - 13, ypos + 5);
    path.lineTo(xpos - 13, ypos - 5);

    painter->fillPath(path, color);

    painter->setPen(Qt::black);

    painter->drawLine(xpos - 10, ypos - 4, xpos - 6, ypos - 4);

    painter->drawLine(xpos - 8, ypos - 4, xpos - 8, ypos + 4);
  }
  else if(rot == 1)
    {
      path.moveTo(xpos + 5, ypos - 10);
      path.lineTo(xpos + 5,  ypos - 5);
      path.lineTo(xpos,     ypos);
      path.lineTo(xpos - 4,  ypos - 5);
      path.lineTo(xpos - 4, ypos - 10);
      path.lineTo(xpos + 5, ypos - 10);

      painter->fillPath(path, color);

      painter->setPen(Qt::black);

      painter->drawLine(xpos - 2, ypos - 8, xpos + 2, ypos - 8);

      painter->drawLine(xpos, ypos - 8, xpos, ypos - 3);
    }
    else if(rot == 2)
      {
        path.moveTo(xpos + 12, ypos - 5);
        path.lineTo(xpos + 5,  ypos - 5);
        path.lineTo(xpos,     ypos);
        path.lineTo(xpos + 5,  ypos + 5);
        path.lineTo(xpos + 12, ypos + 5);
        path.lineTo(xpos + 12, ypos - 5);

        painter->fillPath(path, color);

        painter->setPen(Qt::black);

        painter->drawLine(xpos + 9, ypos - 4, xpos + 5, ypos - 4);

        painter->drawLine(xpos + 7, ypos - 4, xpos + 7, ypos + 4);
      }
}


void SignalCurve::drawTrigCenterArrow(QPainter *painter, int xpos, int ypos)
{
  QPainterPath path;

  path.moveTo(xpos + 7, ypos);
  path.lineTo(xpos - 6, ypos);
  path.lineTo(xpos, ypos + 7);
  path.lineTo(xpos + 7, ypos);
  painter->fillPath(path, QColor(255, 128, 0));
}


void SignalCurve::setSignalColor1(QColor newColor)
{
  SignalColor[0] = newColor;
  update();
}


void SignalCurve::setSignalColor2(QColor newColor)
{
  SignalColor[1] = newColor;
  update();
}


void SignalCurve::setSignalColor3(QColor newColor)
{
  SignalColor[2] = newColor;
  update();
}


void SignalCurve::setSignalColor4(QColor newColor)
{
  SignalColor[3] = newColor;
  update();
}


void SignalCurve::setTraceWidth(int tr_width)
{
  tracewidth = tr_width;
  if(tracewidth < 0)  tracewidth = 0;
  update();
}


void SignalCurve::setBackgroundColor(QColor newColor)
{
  BackgroundColor = newColor;
  update();
}


void SignalCurve::setRasterColor(QColor newColor)
{
  RasterColor = newColor;
  update();
}


void SignalCurve::setBorderSize(int newsize)
{
  bordersize = newsize;
  if(bordersize < 0)  bordersize = 0;
  update();
}


void SignalCurve::mousePressEvent(QMouseEvent *press_event)
{
  int chn,
      m_x,
      m_y;

  setFocus(Qt::MouseFocusReason);

  w = width() - (2 * bordersize);
  h = height() - (2 * bordersize);
#if QT_VERSION < 0x060000
  m_x = press_event->x() - bordersize;
  m_y = press_event->y() - bordersize;
#else
  m_x = press_event->position().x() - bordersize;
  m_y = press_event->position().y() - bordersize;
#endif
  if(devparms == NULL)
  {
    return;
  }

  if(!devparms->connected)
  {
    return;
  }

  if(devparms->math_fft && devparms->math_fft_split)
  {
    m_y -= ((h / 3) + 15);

    if((m_x > -26) && (m_x < 0) && (m_y > (fft_arrow_pos - 7)) && (m_y < (fft_arrow_pos + 7)))
    {
      fft_arrow_moving = 1;
      use_move_events = 1;
      setMouseTracking(true);
      mouse_old_x = m_x;
      mouse_old_y = m_y;
      mainwindow->scrn_timer->stop();
    }

    return;
  }

//  printf("m_x: %i   m_y: %i   trig_pos_arrow_pos: %i\n",m_x, m_y, trig_pos_arrow_pos);

  if(press_event->button() == Qt::LeftButton)
  {
    if(m_y > (h + 12))
    {
//      printf("m_x is: %i   m_y is: %i\n", m_x, m_y);

      m_x += bordersize;

      if((m_x > 8) && (m_x < 118))
      {
        emit chan1Clicked();
      }
      else if((m_x > 133) && (m_x < 243))
        {
          emit chan2Clicked();
        }
        else if((m_x > 258) && (m_x < 368))
          {
            if(devparms->channel_cnt > 2)
            {
              emit chan3Clicked();
            }
          }
          else if((m_x > 383) && (m_x < 493))
            {
              if(devparms->channel_cnt > 3)
              {
                emit chan4Clicked();
              }
            }

      return;
    }

    if(((m_x > (trig_pos_arrow_pos - 8)) && (m_x < (trig_pos_arrow_pos + 8)) && (m_y > 5) && (m_y < 24)) ||
       ((trig_pos_arrow_pos > w) && (m_x > (trig_pos_arrow_pos - 24)) && (m_x <= trig_pos_arrow_pos) && (m_y > 9) && (m_y < 26)) ||
       ((trig_pos_arrow_pos < 0) && (m_x < 24) && (m_x >= 0) && (m_y > 9) && (m_y < 26)))
    {
      trig_pos_arrow_moving = 1;
      use_move_events = 1;
      setMouseTracking(true);
      mouse_old_x = m_x;
      mouse_old_y = m_y;
    }
    else if(((m_x > w) && (m_x < (w + 26)) && (m_y > (trig_level_arrow_pos - 7)) && (m_y < (trig_level_arrow_pos + 7))) ||
            ((trig_level_arrow_pos < 0) && (m_x >= w) && (m_x < (w + 18)) && (m_y >= 0) && (m_y < 22)) ||
            ((trig_level_arrow_pos > h) && (m_x >= w) && (m_x < (w + 18)) && (m_y <= h) && (m_y > (h - 22))))
      {
        if(devparms->triggeredgesource <= TRIG_SRC_CHAN4)
        {
          trig_level_arrow_moving = 1;
          use_move_events = 1;
          trig_line_visible = 1;
          setMouseTracking(true);
          mouse_old_x = m_x;
          mouse_old_y = m_y;
        }
      }
      else
      {
        for(chn=0; chn<devparms->channel_cnt; chn++)
        {
          if(!devparms->chandisplay[chn])
          {
            continue;
          }

          if((m_x > -26 && (m_x < 0) && (m_y > (chan_arrow_pos[chn] - 7)) && (m_y < (chan_arrow_pos[chn] + 7))) ||
             ((chan_arrow_pos[chn] < 0) && (m_x > -18) && (m_x <= 0) && (m_y >= 0) && (m_y < 22)) ||
             ((chan_arrow_pos[chn] > h) && (m_x > -18) && (m_x <= 0) && (m_y <= h) && (m_y > (h - 22))))
          {
            chan_arrow_moving[chn] = 1;
            devparms->activechannel = chn;
            use_move_events = 1;
            setMouseTracking(true);
            mouse_old_x = m_x;
            mouse_old_y = m_y;
            chan_tmp_old_y_pixel_offset[chn] = m_y;

            break;
          }
        }
      }
  }

  if(use_move_events)
  {
    mainwindow->scrn_timer->stop();
  }
}


void SignalCurve::mouseReleaseEvent(QMouseEvent *release_event)
{
  int chn, tmp;

  char str[512];

  double lefttime, righttime, delayrange;

  w = width() - (2 * bordersize);
  h = height() - (2 * bordersize);
#if QT_VERSION < 0x060000
  mouse_x = release_event->x() - bordersize;
  mouse_y = release_event->y() - bordersize;
#else
  mouse_x = release_event->position().x() - bordersize;
  mouse_y = release_event->position().y() - bordersize;
#endif
  if(devparms == NULL)
  {
    return;
  }

  if(!devparms->connected)
  {
    return;
  }

  if(devparms->math_fft && devparms->math_fft_split)
  {
    use_move_events = 0;
    setMouseTracking(false);

    if(fft_arrow_moving)
    {
      fft_arrow_moving = 0;

      if(devparms->screenupdates_on == 1)
      {
        mainwindow->scrn_timer->start(devparms->screentimerival);
      }

      if(devparms->fft_vscale > 9.0)
      {
        devparms->fft_voffset = nearbyint(devparms->fft_voffset);
      }
      else
      {
        devparms->fft_voffset = nearbyint(devparms->fft_voffset * 10.0) / 10.0;
      }

      if(devparms->modelserie == 1)
      {
        snprintf(str, 512, ":MATH:OFFS %e", devparms->fft_voffset);

        mainwindow->set_cue_cmd(str);
      }

      if(devparms->math_fft_unit == 0)
      {
        strlcpy(str, "FFT position: ", 512);

        convert_to_metric_suffix(str + strlen(str), devparms->fft_voffset, 1, 512 - strlen(str));

        strlcat(str, "V/Div", 512);
      }
      else
      {
        snprintf(str, 512, "FFT position: %+.0fdB", devparms->fft_voffset);
      }

      mainwindow->statusLabel->setText(str);

      update();
    }

    return;
  }

  if(trig_pos_arrow_moving)
  {
    trig_pos_arrow_pos = mouse_x;

    if(trig_pos_arrow_pos < 0)
    {
      trig_pos_arrow_pos = 0;
    }

    if(trig_pos_arrow_pos > w)
    {
      trig_pos_arrow_pos = w;
    }

//    printf("w is %i   trig_pos_arrow_pos is %i\n", w, trig_pos_arrow_pos);

    if(devparms->timebasedelayenable)
    {
      devparms->timebasedelayoffset = ((devparms->timebasedelayscale * (double)devparms->hordivisions) / w) * ((w / 2) - trig_pos_arrow_pos);

      tmp = devparms->timebasedelayoffset / (devparms->timebasedelayscale / 50);

      devparms->timebasedelayoffset = (devparms->timebasedelayscale / 50) * tmp;

      lefttime = ((devparms->hordivisions / 2) * devparms->timebasescale) - devparms->timebaseoffset;

      righttime = ((devparms->hordivisions / 2) * devparms->timebasescale) + devparms->timebaseoffset;

      delayrange = (devparms->hordivisions / 2) * devparms->timebasedelayscale;

      if(devparms->timebasedelayoffset < -(lefttime - delayrange))
      {
        devparms->timebasedelayoffset = -(lefttime - delayrange);
      }

      if(devparms->timebasedelayoffset > (righttime - delayrange))
      {
        devparms->timebasedelayoffset = (righttime - delayrange);
      }

      strlcpy(str, "Delayed timebase position: ", 512);

      convert_to_metric_suffix(str + strlen(str), devparms->timebasedelayoffset, 2, 512 - strlen(str));

      strlcat(str, "s", 512);

      mainwindow->statusLabel->setText(str);

      snprintf(str, 512, ":TIM:DEL:OFFS %e", devparms->timebasedelayoffset);

      mainwindow->set_cue_cmd(str);
    }
    else
    {
      devparms->timebaseoffset = ((devparms->timebasescale * (double)devparms->hordivisions) / w) * ((w / 2) - trig_pos_arrow_pos);

      tmp = devparms->timebaseoffset / (devparms->timebasescale / 50);

      devparms->timebaseoffset = (devparms->timebasescale / 50) * tmp;

      strlcpy(str, "Horizontal position: ", 512);

      convert_to_metric_suffix(str + strlen(str), devparms->timebaseoffset, 2, 512 - strlen(str));

      strlcat(str, "s", 512);

      mainwindow->statusLabel->setText(str);

      snprintf(str, 512, ":TIM:OFFS %e", devparms->timebaseoffset);

      mainwindow->set_cue_cmd(str);
    }
  }
  else if(trig_level_arrow_moving)
    {
      if(devparms->triggeredgesource > TRIG_SRC_CHAN4)
      {
        return;
      }

      trig_level_arrow_pos = mouse_y;

      if(trig_level_arrow_pos < 0)
      {
        trig_level_arrow_pos = 0;
      }

      if(trig_level_arrow_pos > h)
      {
        trig_level_arrow_pos = h;
      }

  //       printf("chanoffset[chn] is: %e   chanscale[chn] is %e   trig_level_arrow_pos is: %i   v_sense is: %e\n",
  //              devparms->chanoffset[chn], devparms->chanscale[chn], trig_level_arrow_pos, v_sense);

      devparms->triggeredgelevel[devparms->triggeredgesource] = (((h / 2) - trig_level_arrow_pos) * ((devparms->chanscale[devparms->triggeredgesource] * devparms->vertdivisions) / h))
                                                                - devparms->chanoffset[devparms->triggeredgesource];

      tmp = devparms->triggeredgelevel[devparms->triggeredgesource] / (devparms->chanscale[devparms->triggeredgesource] / 50);

      devparms->triggeredgelevel[devparms->triggeredgesource] = (devparms->chanscale[devparms->triggeredgesource] / 50) * tmp;

      snprintf(str, 512, "Trigger level: ");

      convert_to_metric_suffix(str + strlen(str), devparms->triggeredgelevel[devparms->triggeredgesource], 2, 512 - strlen(str));

      strlcat(str, devparms->chanunitstr[devparms->chanunit[devparms->triggeredgesource]], 512);

      mainwindow->statusLabel->setText(str);

      snprintf(str, 512, ":TRIGger:EDGE:LEVel %e", devparms->triggeredgelevel[devparms->triggeredgesource]);

      mainwindow->set_cue_cmd(str);

      trig_line_timer->start(1300);
    }
    else
    {
      for(chn=0; chn<devparms->channel_cnt; chn++)
      {
        if(!devparms->chandisplay[chn])
        {
          continue;
        }

        if(chan_arrow_moving[chn])
        {
          chan_arrow_pos[chn] = mouse_y;

          if(chan_arrow_pos[chn] < 0)
          {
            chan_arrow_pos[chn] = 0;
          }

          if(chan_arrow_pos[chn] > h)
          {
            chan_arrow_pos[chn] = h;
          }

    //       printf("chanoffset[chn] is: %e   chanscale[chn] is %e   chan_arrow_pos[chn] is: %i   v_sense is: %e\n",
    //              devparms->chanoffset[chn], devparms->chanscale[chn], chan_arrow_pos[chn], v_sense);

          devparms->chanoffset[chn] = ((h / 2) - chan_arrow_pos[chn]) * ((devparms->chanscale[chn] * devparms->vertdivisions) / h);

          tmp = devparms->chanoffset[chn] / (devparms->chanscale[chn] / 50);

          devparms->chanoffset[chn] = (devparms->chanscale[chn] / 50) * tmp;

          snprintf(str, 512, "Channel %i offset: ", chn + 1);

          convert_to_metric_suffix(str + strlen(str), devparms->chanoffset[chn], 3, 512 - strlen(str));

          strlcat(str, devparms->chanunitstr[devparms->chanunit[chn]], 512);

          mainwindow->statusLabel->setText(str);

          snprintf(str, 512, ":CHAN%i:OFFS %e", chn + 1, devparms->chanoffset[chn]);

          mainwindow->set_cue_cmd(str);

          devparms->activechannel = chn;

          chan_tmp_y_pixel_offset[chn] = 0;

          chan_tmp_old_y_pixel_offset[chn] = 0;

          break;
        }
      }
    }

  for(chn=0; chn<MAX_CHNS; chn++)
  {
    chan_arrow_moving[chn] = 0;
  }
  trig_level_arrow_moving = 0;
  trig_pos_arrow_moving = 0;
  use_move_events = 0;
  setMouseTracking(false);

  if(devparms->screenupdates_on == 1)
  {
    mainwindow->scrn_timer->start(devparms->screentimerival);
  }

  update();
}


void SignalCurve::mouseMoveEvent(QMouseEvent *move_event)
{
  int chn, h_fft, a_pos;

  double dtmp;

  if(!use_move_events)
  {
    return;
  }
#if QT_VERSION < 0x060000
  mouse_x = move_event->x() - bordersize;
  mouse_y = move_event->y() - bordersize;
#else
  mouse_x = move_event->position().x() - bordersize;
  mouse_y = move_event->position().y() - bordersize;
#endif
  if(devparms == NULL)
  {
    return;
  }

  if(!devparms->connected)
  {
    return;
  }

  if(devparms->math_fft && devparms->math_fft_split)
  {
    mouse_y -= ((h / 3) + 15);

    h_fft = h * 0.64;

    if(fft_arrow_moving)
    {
      a_pos = mouse_y;

      if(a_pos < 0)
      {
        a_pos = 0;
      }

      if(a_pos > (h * 0.64))
      {
        a_pos = (h * 0.64);
      }

      devparms->fft_voffset = ((h_fft / 2) - a_pos) * (devparms->fft_vscale * 8.0 / h_fft);

      label_active = LABEL_ACTIVE_FFT;

      mainwindow->label_timer->start(LABEL_TIMER_IVAL);
    }

    update();

    return;
  }

  if(trig_pos_arrow_moving)
  {
    trig_pos_arrow_pos = mouse_x;

    if(trig_pos_arrow_pos < 0)
    {
      trig_pos_arrow_pos = 0;
    }

    if(trig_pos_arrow_pos > w)
    {
      trig_pos_arrow_pos = w;
    }
  }
  else if(trig_level_arrow_moving)
    {
      trig_level_arrow_pos = mouse_y;

      if(trig_level_arrow_pos < 0)
      {
        trig_level_arrow_pos = 0;
      }

      if(trig_level_arrow_pos > h)
      {
        trig_level_arrow_pos = h;
      }

      devparms->triggeredgelevel[devparms->triggeredgesource] = (((h / 2) - trig_level_arrow_pos) * ((devparms->chanscale[devparms->triggeredgesource] * devparms->vertdivisions) / h))
                                                                - devparms->chanoffset[devparms->triggeredgesource];

      dtmp = devparms->triggeredgelevel[devparms->triggeredgesource] / (devparms->chanscale[devparms->triggeredgesource] / 50);

      devparms->triggeredgelevel[devparms->triggeredgesource] = (devparms->chanscale[devparms->triggeredgesource] / 50) * dtmp;

      label_active = LABEL_ACTIVE_TRIG;

      mainwindow->label_timer->start(LABEL_TIMER_IVAL);
    }
    else
    {
      for(chn=0; chn<devparms->channel_cnt; chn++)
      {
        if(!devparms->chandisplay[chn])
        {
          continue;
        }

        if(chan_arrow_moving[chn])
        {
          chan_arrow_pos[chn] = mouse_y;

          if(chan_arrow_pos[chn] < 0)
          {
            chan_arrow_pos[chn] = 0;
          }

          if(chan_arrow_pos[chn] > h)
          {
            chan_arrow_pos[chn] = h;
          }

          devparms->chanoffset[chn] = ((h / 2) - chan_arrow_pos[chn]) * ((devparms->chanscale[chn] * devparms->vertdivisions) / h);

//          chan_tmp_y_pixel_offset[chn] = (h / 2) - chan_arrow_pos[chn];

          chan_tmp_y_pixel_offset[chn] = chan_tmp_old_y_pixel_offset[chn] - chan_arrow_pos[chn];

          dtmp = devparms->chanoffset[chn] / (devparms->chanscale[chn] / 50);

          devparms->chanoffset[chn] = (devparms->chanscale[chn] / 50) * dtmp;

          label_active = chn + 1;

          mainwindow->label_timer->start(LABEL_TIMER_IVAL);

          break;
        }
      }
    }

  update();
}


void SignalCurve::trig_line_timer_handler()
{
  trig_line_visible = 0;
}


void SignalCurve::setTrigLineVisible(void)
{
  trig_line_visible = 1;

  trig_line_timer->start(1300);
}


void SignalCurve::trig_stat_timer_handler()
{
  if(!trig_stat_flash)
  {
    trig_stat_timer->stop();

    return;
  }

  if(trig_stat_flash == 1)
  {
    trig_stat_flash = 2;
  }
  else
  {
    trig_stat_flash = 1;
  }
}


void SignalCurve::paintLabel(QPainter *painter, int xpos, int ypos, int xw, int yh, const char *str, QColor color)
{
  QPainterPath path;

  path.addRoundedRect(xpos, ypos, xw, yh, 3, 3);

  painter->fillPath(path, Qt::black);

  painter->setPen(color);

  painter->drawRoundedRect(xpos, ypos, xw, yh, 3, 3);

  painter->drawText(xpos, ypos, xw, yh, Qt::AlignCenter, str);
}


void SignalCurve::paintCounterLabel(QPainter *painter, int xpos, int ypos)
{
  int i;

  char str[512];

  QPainterPath path;

  path.addRoundedRect(xpos, ypos, 175, 20, 3, 3);

  painter->fillPath(path, Qt::black);

  painter->setPen(Qt::darkGray);

  painter->drawRoundedRect(xpos, ypos, 175, 20, 3, 3);

  path = QPainterPath();

  path.addRoundedRect(xpos + 4, ypos + 3, 14, 14, 3, 3);

  painter->fillPath(path, SignalColor[devparms->countersrc - 1]);

  painter->setPen(Qt::black);

  painter->drawLine(xpos + 7, ypos + 6, xpos + 15, ypos + 6);

  painter->drawLine(xpos + 11, ypos + 6, xpos + 11, ypos + 14);

  painter->setPen(Qt::white);

  if((devparms->counterfreq < 15) || (devparms->counterfreq > 1.1e9))
  {
    strlcpy(str, "< 15 Hz", 512);
  }
  else
  {
    convert_to_metric_suffix(str, devparms->counterfreq, 5, 512);

    strlcat(str, "Hz", 512);
  }

  for(i=0; i<3; i++)
  {
    painter->drawLine(xpos + 22 + (i * 14), ypos + 14, xpos + 29 + (i * 14), ypos + 14);
    painter->drawLine(xpos + 29 + (i * 14), ypos + 14, xpos + 29 + (i * 14), ypos + 7);
    painter->drawLine(xpos + 29 + (i * 14), ypos + 7, xpos + 36 + (i * 14), ypos + 7);
    painter->drawLine(xpos + 36 + (i * 14), ypos + 7, xpos + 36 + (i * 14), ypos + 14);
  }
  painter->drawLine(xpos + 22 + (i * 14), ypos + 14, xpos + 29 + (i * 14), ypos + 14);

  painter->drawText(xpos + 75, ypos, 100, 20, Qt::AlignCenter, str);
}


void SignalCurve::paintPlaybackLabel(QPainter *painter, int xpos, int ypos)
{
  char str[512];

  QPainterPath path;

  path.addRoundedRect(xpos, ypos, 175, 20, 3, 3);

  painter->fillPath(path, Qt::black);

  painter->setPen(Qt::darkGray);

  painter->drawRoundedRect(xpos, ypos, 175, 20, 3, 3);

  if(devparms->func_wrec_operate || !devparms->func_has_record)
  {
    painter->fillRect(xpos + 5, ypos + 5, 10, 10, Qt::red);

    painter->setPen(Qt::red);

    snprintf(str, 512, "%i/%i", 0, devparms->func_wrec_fend);

    painter->drawText(xpos + 30, ypos, 120, 20, Qt::AlignCenter, str);
  }
  else
  {
    painter->fillRect(xpos + 5, ypos + 5, 10, 10, Qt::green);

    painter->setPen(Qt::green);

    snprintf(str, 512, "%i/%i", devparms->func_wplay_fcur, devparms->func_wrec_fend);

    painter->drawText(xpos + 30, ypos, 120, 20, Qt::AlignCenter, str);
  }
}


bool SignalCurve::hasMoveEvent(void)
{
  if(use_move_events)
  {
    return true;
  }

  return false;
}


void SignalCurve::draw_decoder(QPainter *painter, int dw, int dh)
{
  int i, j, k,
      cell_width,
      base_line,
      line_h_uart_tx=0,
      line_h_uart_rx=0,
      line_h_spi_mosi=0,
      line_h_spi_miso=0,
      spi_chars=1,
      pixel_per_bit=1;

  double pix_per_smpl;

  char str[512];


  if(devparms->modelserie != 1)
  {
    base_line = (dh / 2) - (((double)dh / 400.0) * devparms->math_decode_pos);
  }
  else
  {
    base_line = ((double)dh / 400.0) * devparms->math_decode_pos;
  }

  pix_per_smpl = (double)dw / (devparms->hordivisions * 100);

  switch(devparms->math_decode_format)
  {
    case 0:  cell_width = 40;  // hex
             break;
    case 1:  cell_width = 30;  // ASCII
             break;
    case 2:  cell_width = 30;  // decimal;
             break;
    case 3:  cell_width = 70;  // binary
             break;
    default: cell_width = 70;  // line
             break;
  }

  if(devparms->math_decode_mode == DECODE_MODE_UART)
  {
    if(devparms->timebasedelayenable)
    {
      pixel_per_bit = ((double)dw / 12.0 / devparms->timebasedelayscale) / (double)devparms->math_decode_uart_baud;
    }
    else
    {
      pixel_per_bit = ((double)dw / 12.0 / devparms->timebasescale) / (double)devparms->math_decode_uart_baud;
    }

    cell_width = pixel_per_bit * devparms->math_decode_uart_width;

    painter->setPen(Qt::green);

    if(devparms->math_decode_uart_tx && devparms->math_decode_uart_rx)
    {
      line_h_uart_tx = base_line - 5;

      line_h_uart_rx = base_line + 45;

      painter->drawLine(0, line_h_uart_tx, dw, line_h_uart_tx);

      painter->drawLine(0, line_h_uart_rx, dw, line_h_uart_rx);
    }
    else if(devparms->math_decode_uart_tx)
      {
        line_h_uart_tx = base_line;

        painter->drawLine(0, line_h_uart_tx, dw, line_h_uart_tx);
      }
      else if(devparms->math_decode_uart_rx)
        {
          line_h_uart_rx = base_line;

          painter->drawLine(0, line_h_uart_rx, dw, line_h_uart_rx);
        }

    if(devparms->math_decode_uart_tx)
    {
      for(i=0; i<devparms->math_decode_uart_tx_nval; i++)
      {
        painter->fillRect(devparms->math_decode_uart_tx_val_pos[i] * pix_per_smpl, line_h_uart_tx - 13, cell_width, 26, Qt::black);

        painter->drawRect(devparms->math_decode_uart_tx_val_pos[i] * pix_per_smpl, line_h_uart_tx - 13, cell_width, 26);
      }
    }

    if(devparms->math_decode_uart_rx)
    {
      for(i=0; i<devparms->math_decode_uart_rx_nval; i++)
      {
        painter->fillRect(devparms->math_decode_uart_rx_val_pos[i] * pix_per_smpl, line_h_uart_rx - 13, cell_width, 26, Qt::black);

        painter->drawRect(devparms->math_decode_uart_rx_val_pos[i] * pix_per_smpl, line_h_uart_rx - 13, cell_width, 26);
      }
    }

    painter->setPen(Qt::white);

    if(devparms->math_decode_uart_tx)
    {
      switch(devparms->math_decode_format)
      {
        case 0: painter->drawText(5, line_h_uart_tx - 35, 65, 30, Qt::AlignCenter, "Tx[HEX]");
                break;
        case 1: painter->drawText(5, line_h_uart_tx - 35, 65, 30, Qt::AlignCenter, "Tx[ASC]");
                break;
        case 2: painter->drawText(5, line_h_uart_tx - 35, 65, 30, Qt::AlignCenter, "Tx[DEC]");
                break;
        case 3: painter->drawText(5, line_h_uart_tx - 35, 65, 30, Qt::AlignCenter, "Tx[BIN]");
                break;
        case 4: painter->drawText(5, line_h_uart_tx - 35, 65, 30, Qt::AlignCenter, "Tx[LINE]");
                break;
        default: painter->drawText(5, line_h_uart_tx - 35, 65, 30, Qt::AlignCenter, "Tx[\?\?\?]");
                break;
      }

      for(i=0; i<devparms->math_decode_uart_tx_nval; i++)
      {
        if(devparms->math_decode_format == 0)  // hex
        {
          snprintf(str, 512, "%02X", devparms->math_decode_uart_tx_val[i]);

          painter->drawText(devparms->math_decode_uart_tx_val_pos[i] * pix_per_smpl, line_h_uart_tx - 13, cell_width, 30, Qt::AlignCenter, str);
        }
        else if(devparms->math_decode_format == 1)  // ASCII
          {
            ascii_decode_control_char(devparms->math_decode_uart_tx_val[i], str, 512);

            painter->drawText(devparms->math_decode_uart_tx_val_pos[i] * pix_per_smpl, line_h_uart_tx - 13, cell_width, 30, Qt::AlignCenter, str);
          }
          else if(devparms->math_decode_format == 2)  // decimal
            {
              snprintf(str, 512, "%u", (unsigned int)devparms->math_decode_uart_tx_val[i]);

              painter->drawText(devparms->math_decode_uart_tx_val_pos[i] * pix_per_smpl, line_h_uart_tx - 13, cell_width, 30, Qt::AlignCenter, str);
            }
            else if(devparms->math_decode_format == 3)  // binary
              {
                for(j=0; j<devparms->math_decode_uart_width; j++)
                {
                  str[devparms->math_decode_uart_width - 1 - j] = ((devparms->math_decode_uart_tx_val[i] >> j) & 1) + '0';
                }

                str[j] = 0;

                painter->drawText(devparms->math_decode_uart_tx_val_pos[i] * pix_per_smpl, line_h_uart_tx - 13, cell_width, 30, Qt::AlignCenter, str);
              }
              else if(devparms->math_decode_format == 4)  // line
                {
                  for(j=0; j<devparms->math_decode_uart_width; j++)
                  {
                    str[j] = ((devparms->math_decode_uart_tx_val[i] >> j) & 1) + '0';
                  }

                  str[j] = 0;

                  painter->drawText(devparms->math_decode_uart_tx_val_pos[i] * pix_per_smpl, line_h_uart_tx - 13, cell_width, 30, Qt::AlignCenter, str);
                }

          if(devparms->math_decode_uart_tx_err[i])
          {
            painter->setPen(Qt::red);

            painter->drawText(devparms->math_decode_uart_tx_val_pos[i] * pix_per_smpl + cell_width, line_h_uart_tx - 13, 25, 25, Qt::AlignCenter, "?");

            painter->setPen(Qt::white);
          }
      }
    }

    if(devparms->math_decode_uart_rx)
    {
      switch(devparms->math_decode_format)
      {
        case 0: painter->drawText(5, line_h_uart_rx - 35, 65, 30, Qt::AlignCenter, "Rx[HEX]");
                break;
        case 1: painter->drawText(5, line_h_uart_rx - 35, 65, 30, Qt::AlignCenter, "Rx[ASC]");
                break;
        case 2: painter->drawText(5, line_h_uart_rx - 35, 65, 30, Qt::AlignCenter, "Rx[DEC]");
                break;
        case 3: painter->drawText(5, line_h_uart_rx - 35, 65, 30, Qt::AlignCenter, "Rx[BIN]");
                break;
        case 4: painter->drawText(5, line_h_uart_rx - 35, 65, 30, Qt::AlignCenter, "Rx[LINE]");
                break;
        default: painter->drawText(5, line_h_uart_rx - 35, 65, 30, Qt::AlignCenter, "Rx[\?\?\?]");
                break;
      }

      for(i=0; i<devparms->math_decode_uart_rx_nval; i++)
      {
        if(devparms->math_decode_format == 0)  // hex
        {
          snprintf(str, 512, "%02X", devparms->math_decode_uart_rx_val[i]);

          painter->drawText(devparms->math_decode_uart_rx_val_pos[i] * pix_per_smpl, line_h_uart_rx - 13, cell_width, 30, Qt::AlignCenter, str);
        }
        else if(devparms->math_decode_format == 1)  // ASCII
          {
            ascii_decode_control_char(devparms->math_decode_uart_rx_val[i], str, 512);

            painter->drawText(devparms->math_decode_uart_rx_val_pos[i] * pix_per_smpl, line_h_uart_rx - 13, cell_width, 30, Qt::AlignCenter, str);
          }
          else if(devparms->math_decode_format == 2)  // decimal
            {
              snprintf(str, 512, "%u", (unsigned int)devparms->math_decode_uart_rx_val[i]);

              painter->drawText(devparms->math_decode_uart_rx_val_pos[i] * pix_per_smpl, line_h_uart_rx - 13, cell_width, 30, Qt::AlignCenter, str);
            }
            else if(devparms->math_decode_format == 3)  // binary
              {
                for(j=0; j<devparms->math_decode_uart_width; j++)
                {
                  str[devparms->math_decode_uart_width - 1 - j] = ((devparms->math_decode_uart_rx_val[i] >> j) & 1) + '0';
                }

                str[j] = 0;

                painter->drawText(devparms->math_decode_uart_rx_val_pos[i] * pix_per_smpl, line_h_uart_rx - 13, cell_width, 30, Qt::AlignCenter, str);
              }
              else if(devparms->math_decode_format == 4)  // line
                {
                  for(j=0; j<devparms->math_decode_uart_width; j++)
                  {
                    str[j] = ((devparms->math_decode_uart_rx_val[i] >> j) & 1) + '0';
                  }

                  str[j] = 0;

                  painter->drawText(devparms->math_decode_uart_rx_val_pos[i] * pix_per_smpl, line_h_uart_rx - 13, cell_width, 30, Qt::AlignCenter, str);
                }

          if(devparms->math_decode_uart_rx_err[i])
          {
            painter->setPen(Qt::red);

            painter->drawText(devparms->math_decode_uart_rx_val_pos[i] * pix_per_smpl + cell_width, line_h_uart_rx - 13, 25, 25, Qt::AlignCenter, "?");

            painter->setPen(Qt::white);
          }
      }
    }
  }

  if(devparms->math_decode_mode == DECODE_MODE_SPI)
  {
    painter->setPen(Qt::green);

    if(devparms->math_decode_spi_width > 24)
    {
      spi_chars = 4;
    }
    else if(devparms->math_decode_spi_width > 16)
      {
        spi_chars = 3;
      }
      else if(devparms->math_decode_spi_width > 8)
        {
          spi_chars = 2;
        }
        else
        {
          spi_chars = 1;
        }

    cell_width *= spi_chars;

    if(devparms->math_decode_spi_mosi && devparms->math_decode_spi_miso)
    {
      line_h_spi_mosi = base_line - 5;

      line_h_spi_miso = base_line + 45;

      painter->drawLine(0, line_h_spi_mosi, dw, line_h_spi_mosi);

      painter->drawLine(0, line_h_spi_miso, dw, line_h_spi_miso);
    }
    else if(devparms->math_decode_spi_mosi)
      {
        line_h_spi_mosi = base_line;

        painter->drawLine(0, line_h_spi_mosi, dw, line_h_spi_mosi);
      }
      else if(devparms->math_decode_spi_miso)
        {
          line_h_spi_miso = base_line;

          painter->drawLine(0, line_h_spi_miso, dw, line_h_spi_miso);
        }

    if(devparms->math_decode_spi_mosi)
    {
      for(i=0; i<devparms->math_decode_spi_mosi_nval; i++)
      {
        cell_width = (devparms->math_decode_spi_mosi_val_pos_end[i] - devparms->math_decode_spi_mosi_val_pos[i]) *
                      pix_per_smpl;

        painter->fillRect(devparms->math_decode_spi_mosi_val_pos[i] * pix_per_smpl, line_h_spi_mosi - 13, cell_width, 26, Qt::black);

        painter->drawRect(devparms->math_decode_spi_mosi_val_pos[i] * pix_per_smpl, line_h_spi_mosi - 13, cell_width, 26);
      }
    }

    if(devparms->math_decode_spi_miso)
    {
      for(i=0; i<devparms->math_decode_spi_miso_nval; i++)
      {
        cell_width = (devparms->math_decode_spi_miso_val_pos_end[i] - devparms->math_decode_spi_miso_val_pos[i]) *
                      pix_per_smpl;

        painter->fillRect(devparms->math_decode_spi_miso_val_pos[i] * pix_per_smpl, line_h_spi_miso - 13, cell_width, 26, Qt::black);

        painter->drawRect(devparms->math_decode_spi_miso_val_pos[i] * pix_per_smpl, line_h_spi_miso - 13, cell_width, 26);
      }
    }

    painter->setPen(Qt::white);

    if(devparms->math_decode_spi_mosi)
    {
      switch(devparms->math_decode_format)
      {
        case 0: painter->drawText(5, line_h_spi_mosi - 35, 80, 30, Qt::AlignCenter, "Mosi[HEX]");
                break;
        case 1: painter->drawText(5, line_h_spi_mosi - 35, 80, 30, Qt::AlignCenter, "Mosi[ASC]");
                break;
        case 2: painter->drawText(5, line_h_spi_mosi - 35, 80, 30, Qt::AlignCenter, "Mosi[DEC]");
                break;
        case 3: painter->drawText(5, line_h_spi_mosi - 35, 80, 30, Qt::AlignCenter, "Mosi[BIN]");
                break;
        case 4: painter->drawText(5, line_h_spi_mosi - 35, 80, 30, Qt::AlignCenter, "Mosi[LINE]");
                break;
        default: painter->drawText(5, line_h_spi_mosi - 35, 80, 30, Qt::AlignCenter, "Mosi[\?\?\?]");
                break;
      }

      for(i=0; i<devparms->math_decode_spi_mosi_nval; i++)
      {
        if(devparms->math_decode_format == 0)  // hex
        {
          switch(spi_chars)
          {
            case 1: snprintf(str, 512, "%02X", devparms->math_decode_spi_mosi_val[i]);
                    break;
            case 2: snprintf(str, 512, "%04X", devparms->math_decode_spi_mosi_val[i]);
                    break;
            case 3: snprintf(str, 512, "%06X", devparms->math_decode_spi_mosi_val[i]);
                    break;
            case 4: snprintf(str, 512, "%08X", devparms->math_decode_spi_mosi_val[i]);
                    break;
          }

          painter->drawText(devparms->math_decode_spi_mosi_val_pos[i] * pix_per_smpl, line_h_spi_mosi - 13, cell_width, 30, Qt::AlignCenter, str);
        }
        else if(devparms->math_decode_format == 1)  // ASCII
          {
            for(k=0, j=0; k<spi_chars; k++)
            {
              j += ascii_decode_control_char(devparms->math_decode_spi_mosi_val[i] >> (k * 8), str + j, 512 - j);
            }

            painter->drawText(devparms->math_decode_spi_mosi_val_pos[i] * pix_per_smpl, line_h_spi_mosi - 13, cell_width, 30, Qt::AlignCenter, str);
          }
          else if(devparms->math_decode_format == 2)  // decimal
            {
              snprintf(str, 512, "%u", devparms->math_decode_spi_mosi_val[i]);

              painter->drawText(devparms->math_decode_spi_mosi_val_pos[i] * pix_per_smpl, line_h_spi_mosi - 13, cell_width, 30, Qt::AlignCenter, str);
            }
            else if(devparms->math_decode_format == 3)  // binary
              {
                for(j=0; j<devparms->math_decode_spi_width; j++)
                {
                  str[devparms->math_decode_spi_width - 1 - j] = ((devparms->math_decode_spi_mosi_val[i] >> j) & 1) + '0';
                }

                str[j] = 0;

                painter->drawText(devparms->math_decode_spi_mosi_val_pos[i] * pix_per_smpl, line_h_spi_mosi - 13, cell_width, 30, Qt::AlignCenter, str);
              }
              else if(devparms->math_decode_format == 4)  // line
                {
                  for(j=0; j<devparms->math_decode_spi_width; j++)
                  {
                    str[j] = ((devparms->math_decode_spi_mosi_val[i] >> j) & 1) + '0';
                  }

                  str[devparms->math_decode_spi_width] = 0;

                  painter->drawText(devparms->math_decode_spi_mosi_val_pos[i] * pix_per_smpl, line_h_spi_mosi - 13, cell_width, 30, Qt::AlignCenter, str);
                }
      }
    }

    if(devparms->math_decode_spi_miso)
    {
      switch(devparms->math_decode_format)
      {
        case 0: painter->drawText(5, line_h_spi_miso - 35, 80, 30, Qt::AlignCenter, "Miso[HEX]");
                break;
        case 1: painter->drawText(5, line_h_spi_miso - 35, 80, 30, Qt::AlignCenter, "Miso[HEX]");
                break;
        case 2: painter->drawText(5, line_h_spi_miso - 35, 80, 30, Qt::AlignCenter, "Miso[DEC]");
                break;
        case 3: painter->drawText(5, line_h_spi_miso - 35, 80, 30, Qt::AlignCenter, "Miso[BIN]");
                break;
        case 4: painter->drawText(5, line_h_spi_miso - 35, 80, 30, Qt::AlignCenter, "Miso[LINE]");
                break;
        default: painter->drawText(5, line_h_spi_miso - 35, 80, 30, Qt::AlignCenter, "Miso[\?\?\?]");
                break;
      }

      for(i=0; i<devparms->math_decode_spi_miso_nval; i++)
      {
        if(devparms->math_decode_format == 0)  // hex
        {
          switch(spi_chars)
          {
            case 1: snprintf(str, 512, "%02X", devparms->math_decode_spi_miso_val[i]);
                    break;
            case 2: snprintf(str, 512, "%04X", devparms->math_decode_spi_miso_val[i]);
                    break;
            case 3: snprintf(str, 512, "%06X", devparms->math_decode_spi_miso_val[i]);
                    break;
            case 4: snprintf(str, 512, "%08X", devparms->math_decode_spi_miso_val[i]);
                    break;
          }

          painter->drawText(devparms->math_decode_spi_miso_val_pos[i] * pix_per_smpl, line_h_spi_miso - 13, cell_width, 30, Qt::AlignCenter, str);
        }
        else if(devparms->math_decode_format == 1)  // ASCII
          {
            for(k=0, j=0; k<spi_chars; k++)
            {
              j += ascii_decode_control_char(devparms->math_decode_spi_miso_val[i] >> (k * 8), str + j, 512 - j);
            }

            painter->drawText(devparms->math_decode_spi_miso_val_pos[i] * pix_per_smpl, line_h_spi_miso - 13, cell_width, 30, Qt::AlignCenter, str);
          }
          else if(devparms->math_decode_format == 2)  // decimal
            {
              snprintf(str, 512, "%u", devparms->math_decode_spi_miso_val[i]);

              painter->drawText(devparms->math_decode_spi_miso_val_pos[i] * pix_per_smpl, line_h_spi_miso - 13, cell_width, 30, Qt::AlignCenter, str);
            }
            else if(devparms->math_decode_format == 3)  // binary
              {
                for(j=0; j<devparms->math_decode_spi_width; j++)
                {
                  str[devparms->math_decode_spi_width - 1 - j] = ((devparms->math_decode_spi_miso_val[i] >> j) & 1) + '0';
                }

                str[j] = 0;

                painter->drawText(devparms->math_decode_spi_miso_val_pos[i] * pix_per_smpl, line_h_spi_miso - 13, cell_width, 30, Qt::AlignCenter, str);
              }
              else if(devparms->math_decode_format == 4)  // line
                {
                  for(j=0; j<devparms->math_decode_spi_width; j++)
                  {
                    str[j] = ((devparms->math_decode_spi_miso_val[i] >> j) & 1) + '0';
                  }

                  str[devparms->math_decode_spi_width] = 0;

                  painter->drawText(devparms->math_decode_spi_miso_val_pos[i] * pix_per_smpl, line_h_spi_miso - 13, cell_width, 30, Qt::AlignCenter, str);
                }
      }
    }
  }
}


int SignalCurve::ascii_decode_control_char(char ch, char *str, int sz)
{
  if((ch > 32) && (ch < 127))
  {
    str[0] = ch;

    str[1] = 0;

    return 1;
  }

  switch(ch)
  {
    case  0: strlcpy(str, "NULL", sz);
             break;
    case  1: strlcpy(str, "SOH", sz);
             break;
    case  2: strlcpy(str, "STX", sz);
             break;
    case  3: strlcpy(str, "ETX", sz);
             break;
    case  4: strlcpy(str, "EOT", sz);
             break;
    case  5: strlcpy(str, "ENQ", sz);
             break;
    case  6: strlcpy(str, "ACK", sz);
             break;
    case  7: strlcpy(str, "BEL", sz);
             break;
    case  8: strlcpy(str, "BS", sz);
             break;
    case  9: strlcpy(str, "HT", sz);
             break;
    case 10: strlcpy(str, "LF", sz);
             break;
    case 11: strlcpy(str, "VT", sz);
             break;
    case 12: strlcpy(str, "FF", sz);
             break;
    case 13: strlcpy(str, "CR", sz);
             break;
    case 14: strlcpy(str, "SO", sz);
             break;
    case 15: strlcpy(str, "SI", sz);
             break;
    case 16: strlcpy(str, "DLE", sz);
             break;
    case 17: strlcpy(str, "DC1", sz);
             break;
    case 18: strlcpy(str, "DC2", sz);
             break;
    case 19: strlcpy(str, "DC3", sz);
             break;
    case 20: strlcpy(str, "DC4", sz);
             break;
    case 21: strlcpy(str, "NAK", sz);
             break;
    case 22: strlcpy(str, "SYN", sz);
             break;
    case 23: strlcpy(str, "ETB", sz);
             break;
    case 24: strlcpy(str, "CAN", sz);
             break;
    case 25: strlcpy(str, "EM", sz);
             break;
    case 26: strlcpy(str, "SUB", sz);
             break;
    case 27: strlcpy(str, "ESC", sz);
             break;
    case 28: strlcpy(str, "FS", sz);
             break;
    case 29: strlcpy(str, "GS", sz);
             break;
    case 30: strlcpy(str, "RS", sz);
             break;
    case 31: strlcpy(str, "US", sz);
             break;
    case 32: strlcpy(str, "SP", sz);
             break;
    case 127: strlcpy(str, "DEL", sz);
             break;
    default: strlcpy(str, ".", sz);
             break;
  }

  return strlen(str);
}












