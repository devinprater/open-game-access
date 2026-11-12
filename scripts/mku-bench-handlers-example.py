# handlers.py: reloaded on every call. Remote control: if cmd.py exists it is exec'd once, then renamed.
import os


def handle(low, ctx):
    p = '/home/devin/mku-bench/cmd.py'
    if os.path.exists(p):
        code = open(p).read()
        os.rename(p, p + '.done')
        ns = dict(ctx)
        try:
            exec(code, ns)
            ctx['log']('cmd.py ran')
        except Exception as e:
            ctx['log']('cmd.py error: %r' % (e,))
    else:
        ctx['time'].sleep(0.4)
    return True
