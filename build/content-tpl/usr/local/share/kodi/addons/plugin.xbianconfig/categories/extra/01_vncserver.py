from __future__ import print_function

try:
    from future_builtins import map
    import itertools.ifilter as filter
except ImportError:
    pass

import re

from resources.lib.xbmcguie.xbmcContainer import *
from resources.lib.xbmcguie.xbmcControl import *
from resources.lib.xbmcguie.tag import Tag
from resources.lib.xbmcguie.category import Setting

import resources.lib.translation
_ = resources.lib.translation.language.gettext

class vncServer(Setting):
    CONTROL = CategoryLabelControl(Tag('label','VNC Server'))

class enablePassword(Setting):
    CONTROL = RadioButtonControl(Tag('label',_('Enable VNC password')))
    DIALOGHEADER = _('VNC password')

    def onInit(self):
        self.cfgfile = '/etc/default/vnc-server'
        self.setting = 'USEPASS'
        self.exist = False

    def getUserValue(self):
        return str(self.getControlValue())

    def setControlValue(self,value):
        if value == '1' or value == 'True':
            value = True
        else:
            value = False
        self.control.setValue(value)

    def getXbianValue(self):
        with open(self.cfgfile,'r') as f:
            #mat = [x for x in f.readlines() if re.match('%s=.*'%self.setting,x)]
            mat = list(filter(lambda x: re.match('%s=.*'%self.setting,x),f.readlines()))
        if mat:
            self.exist = True
            return re.search('[01]',mat[0]).group()[0]
        return 0

    def setXbianValue(self,value):
        if value == True or value == 'True':
            value = '1'
        elif value == False or value == 'False':
            value = '0'
        if self.exist and value in ('0','1'):
            #replace
            def replace(x):
                if re.match('%s=.*'%self.setting,x):
                    return re.sub('[01]',value,x,1)
                else:
                    return x
            with open(self.cfgfile, "r") as f:
                data = list(map(replace,open(self.cfgfile,'r').readlines()))
            with open(self.cfgfile, "w") as f:
                f.writelines(data)
        elif value in ('0','1'):
            with open(self.cfgfile, "a") as f:
                f.write('%s=%s\n'%(self.setting,value))
        else:
            return False
        self.DIALOGHEADER = _('VNC server')
        self.OKTEXT = _('Please restart %s') % (self.DIALOGHEADER)
        return True

class changeResolution(Setting):
    CONTROL = RadioButtonControl(Tag('label',_('Switch resolution when %s starts') % _('VNC server')))
    DIALOGHEADER = _('VNC server')

    def onInit(self):
        self.cfgfile = '/etc/default/vnc-autores'
        self.setting = 'AUTOMODE'
        self.exist = False

    def getUserValue(self):
        return str(self.getControlValue())

    def setControlValue(self,value):
        if value == '1' or value == 'True':
            value = True
        else:
            value = False
        self.control.setValue(value)

    def getXbianValue(self):
        with open(self.cfgfile,'r') as f:
            #mat = [x for x in f.readlines() if re.match('%s=.*'%self.setting,x)]
            mat = list(filter(lambda x: re.match('%s=.*'%self.setting,x),f.readlines()))
        if mat:
            self.exist = True
            return re.search('[01]',mat[0]).group()[0]
        return 0

    def setXbianValue(self,value):
        if value == True or value == 'True':
            value = '1'
        elif value == False or value == 'False':
            value = '0'
        if self.exist and value in ('0','1'):
            #replace
            def replace(x):
                if re.match('%s=.*'%self.setting,x):
                    return re.sub('[01]',value,x,1)
                else:
                    return x
            with open(self.cfgfile, "r") as f:
                data = list(map(replace,open(self.cfgfile,'r').readlines()))
            with open(self.cfgfile, "w") as f:
                f.writelines(data)
        elif value in ('0','1'):
            with open(self.cfgfile, "a") as f:
                f.write('%s=%s\n'%(self.setting,value))
        else:
            return False
        return True

settings = [vncServer, enablePassword, changeResolution]
