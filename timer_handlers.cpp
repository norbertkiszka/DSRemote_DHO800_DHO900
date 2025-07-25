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




void UI_Mainwindow::test_timer_handler()
{
  printf("scr_update_cntr is: %u\n", waveForm->scr_update_cntr);

  waveForm->scr_update_cntr = 0;
}


void UI_Mainwindow::label_timer_handler()
{
  waveForm->label_active = LABEL_ACTIVE_NONE;
}


void UI_Mainwindow::navDial_timer_handler()
{
  if(navDial->isSliderDown() == true)
  {
    navDialChanged(navDial->value());
  }
  else
  {
    navDial->setSliderPosition(50);

    navDialFunc = NAV_DIAL_FUNC_NONE;

    if(adjdial_timer->isActive() == false)
    {
      adjDialFunc = ADJ_DIAL_FUNC_NONE;
    }
  }
}


void UI_Mainwindow::adjdial_timer_handler()
{
  char str[512];

  adjdial_timer->stop();

  adjDialLabel->setStyleSheet(def_stylesh);

  adjDialLabel->setStyleSheet("font: 7pt;");

  adjDialLabel->setText("");

  if(adjDialFunc == ADJ_DIAL_FUNC_HOLDOFF)
  {
    strlcpy(str, "Trigger holdoff: ", 512);

    convert_to_metric_suffix(str + strlen(str), devparms.triggerholdoff, 2, 512 - strlen(str));

    strlcat(str, "s", 512);

    statusLabel->setText(str);

    snprintf(str, 512, ":TRIG:HOLD %e", devparms.triggerholdoff);

    set_cue_cmd(str);

    if((devparms.modelserie == 2) || (devparms.modelserie == 6))
    {
      usleep(20000);

      set_cue_cmd(":DISP:CLE");
    }
    else
    {
      usleep(20000);

      set_cue_cmd(":CLE");
    }
  }
  else if(adjDialFunc == ADJ_DIAL_FUNC_ACQ_AVG)
    {
      snprintf(str, 512, "Acquire averages: %i", devparms.acquireaverages);

      statusLabel->setText(str);

      snprintf(str, 512, ":ACQ:AVER %i", devparms.acquireaverages);

      set_cue_cmd(str);
    }

  adjDialFunc = ADJ_DIAL_FUNC_NONE;

  if(navDial_timer->isActive() == false)
  {
    navDialFunc = NAV_DIAL_FUNC_NONE;
  }
}


void UI_Mainwindow::scrn_timer_handler()
{
  if(pthread_mutex_trylock(&devparms.mutexx))
  {
    return;
  }

  scrn_thread->set_params(&devparms);

  scrn_thread->start();
}


void UI_Mainwindow::horPosDial_timer_handler()
{
  char str[512];

  if(devparms.timebasedelayenable)
  {
    snprintf(str, 512, ":TIM:DEL:OFFS %e", devparms.timebasedelayoffset);
  }
  else
  {
    snprintf(str, 512, ":TIM:OFFS %e", devparms.timebaseoffset);
  }

  set_cue_cmd(str);
}


void UI_Mainwindow::trigAdjDial_timer_handler()
{
  int chn;

  char str[512];

  chn = devparms.triggeredgesource;

  if((chn < 0) || (chn > 3))
  {
    return;
  }

  snprintf(str, 512, ":TRIGger:EDGE:LEVel %e", devparms.triggeredgelevel[chn]);

  set_cue_cmd(str);
}


void UI_Mainwindow::vertOffsDial_timer_handler()
{
  int chn;

  char str[512];

  if(devparms.activechannel < 0)
  {
    return;
  }

  chn = devparms.activechannel;

  snprintf(str, 512, ":CHAN%i:OFFS %e", chn + 1, devparms.chanoffset[chn]);

  set_cue_cmd(str);
}


void UI_Mainwindow::horScaleDial_timer_handler()
{
  char str[512];

  if(devparms.timebasedelayenable)
  {
    snprintf(str, 512, ":TIM:DEL:SCAL %e", devparms.timebasedelayscale);
  }
  else
  {
    snprintf(str, 512, ":TIM:SCAL %e", devparms.timebasescale);
  }

  set_cue_cmd(str);
}


void UI_Mainwindow::vertScaleDial_timer_handler()
{
  int chn;

  char str[512];

  if(devparms.activechannel < 0)
  {
    return;
  }

  chn = devparms.activechannel;

  snprintf(str, 512, ":CHAN%i:SCAL %e", chn + 1, devparms.chanscale[chn]);

  set_cue_cmd(str);
}




















