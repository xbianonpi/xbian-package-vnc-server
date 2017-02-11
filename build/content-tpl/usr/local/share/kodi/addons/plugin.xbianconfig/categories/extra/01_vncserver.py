import re

from resources.lib.xbmcguie.xbmcContainer import *
from resources.lib.xbmcguie.xbmcControl import *
from resources.lib.xbmcguie.tag import Tag
from resources.lib.xbmcguie.category import Setting

import resources.lib.translation
_ = resources.lib.translation.language.ugettext

class vncServer(Setting) :
    CONTROL = CategoryLabelControl(Tag('label','Vnc Serveur'))

class changeresolution(Setting) :
    CONTROL = RadioButtonControl(Tag('label','Switch resolution when vnc start'))
    DIALOGHEADER = _('Vnc Server')
                    
    def onInit(self):
        self.cfgfile = '/etc/default/vnc-autores'
        self.setting = 'AUTOMODE'
        self.exist = False
        
    def getUserValue(self):
        return str(self.getControlValue())
    
    def setControlValue(self,value) :
        if value == '1' :
            value = True
        else :
            value = False
        self.control.setValue(value)
    
    def getXbianValue(self):   
        with open(self.cfgfile,'r') as f :             
            mat = filter(lambda x : re.match('%s=.*'%self.setting,x),f.readlines())            
        if mat :            
            self.exist = True            
            return re.search('[01]',mat[0]).group()[0]
        return 0
        
    def setXbianValue(self,value):      
        if self.exist and value in ('0','1'):
            #replace
            def replace(x) :
                print x
                if re.match('%s=.*'%self.setting,x) :                    
                    return re.sub('[01]',value,x,1)
                else :
                    return x
            with open(self.cfgfile, "r") as f:                
                data = map(replace,open(self.cfgfile,'r').readlines())
            with open(self.cfgfile, "w") as f:                
                f.writelines(data) 
        elif value in ('0','1'):
            with open(self.cfgfile, "a") as f:
                f.write('%s=%s\n'%(self.setting,value))                
        else :
            return False
        return True
                
settings = [vncServer,changeresolution]
