from mininet.cli import CLI
from mininet.net import Mininet
from mininet.link import TCLink
from mininet.topo import Topo
from mininet.log import setLogLevel

class AssignmentNetworks(Topo):
    def __init__(self, **opts):
        Topo.__init__(self, **opts)
    #Set up Network Here
    
        # Add hosts
        h1 = self.addHost('h1')
        h2 = self.addHost('h2')
        h3 = self.addHost('h3')
        h4 = self.addHost('h4')
        h5 = self.addHost('h5')
        h6 = self.addHost('h6')
        h7 = self.addHost('h7')
        h8 = self.addHost('h8')
        h9 = self.addHost('h9')
        h10 = self.addHost('h10')

        # Add switches
        s1 = self.addSwitch('s1')
        s2 = self.addSwitch('s2')
        s3 = self.addSwitch('s3')
        s4 = self.addSwitch('s4')
        s5 = self.addSwitch('s5')
        s6 = self.addSwitch('s6')

        # Add links
        self.addLink(h1, s1)
        self.addLink(h2, s4)
        self.addLink(h3, s1)
        self.addLink(h4, s1)
        self.addLink(h5, s4)
        self.addLink(h6, s2)
        self.addLink(h7, s5)
        self.addLink(h8, s3)
        self.addLink(h9, s6)
        self.addLink(h10, s6)
        self.addLink(s1, s2, bw=20, delay='40ms')
        self.addLink(s2, s3, bw=30, delay='20ms')
        self.addLink(s2, s5, bw=25, delay='5ms')
        self.addLink(s1, s4, bw=40, delay='50ms')
        self.addLink(s5, s6, bw=40, delay='5ms')
        
if __name__ == '__main__':
    setLogLevel( 'info' )

    # Create data network
    topo = AssignmentNetworks()
    net = Mininet(topo=topo, link=TCLink, autoSetMacs=True,
           autoStaticArp=True)

    # Run network
    net.start()
    CLI( net )
    net.stop()
