
with open(r'C:\Users\Administrator\.openclaw\workspace\bootmanager\src\core\wimboot.c', 'rb') as f:
    c = f.read().decode('utf-8')

print('bcdedit refs:', c.count('bcdedit'))
print('RefindConfigAddMenuEntry refs:', c.count('RefindConfigAddMenuEntry'))
print('WimSelectFileDialog present:', 'WimSelectFileDialog' in c)

if 'refind_config.h' not in c:
    old = '#include "wimboot.h"'
    new = '#include "wimboot.h"\n#include "refind_config.h"'
    c = c.replace(old, new, 1)
    with open(r'C:\Users\Administrator\.openclaw\workspace\bootmanager\src\core\wimboot.c', 'wb') as f:
        f.write(c.encode('utf-8'))
    print('Added refind_config.h include')
