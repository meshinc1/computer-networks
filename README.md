# Computer Networks Course Projects

### Brief

This repository contains all projects undertaken during my Spring 2022 Computer Networks course. As part of the curriculum, students were tasked with three programming projects, offering practical applications of networking principles. These projects facilitated hands-on experience in socket programming and involved in-depth exploration of protocols including TCP/IP and BitTorrent.

### Project 1: Intro to Socket Programming and Mininet

#### Description

The aim of Project 1 was to become comfortable with socket programming and learn to write applications that use sockets to send information across a network. The task involved developing a tool capable of sending and receiving TCP packets between two hosts, functioning as a client and server, respectively.

In client mode, the tool transmits TCP packets to the server within a specified timeframe, tracking the data sent to calculate the connection's bandwidth. Conversely, in server mode, the tool receives TCP packets and monitors the incoming data to compute the connection's bandwith based on the duration between receiving the first and last byte of data.

The configuration of the virtual network topology was achieved using [Mininet](https://mininet.org/), a network emulator used to develop software-defined networks. The provided topology, defined in the `/'python scripts'` directory, establishes ten hosts and six network switches interconnected with links of varying bandwidth and latency. 

[Topology Diagram]

The defined Mininet topology follows the above diagram.

#### Result

The `/measurements` directory contains the results of the latency and throughput measurements for each link in the network. To ensure consistency, latency tests were conducted with 30 packets, and throughput tests were performed over a duration of 30 seconds. As anticipated, the measured latency of each link positively correlates with the defined delay period, with `round-trip-time` approximately twice the defined `delay`. Similarly, measured throughput demonstrates a positive correlation with defined bandwidth, with higher bandwidth resulting in increased transmission rates and greater total data transfer.

### Project 2: Reliable Transport Built On UDP

#### Description

Project 2 was an opportunity to gain a thorough understanding of the TCP and UDP transport protocols, delving into their respective use cases and functionalities. The goal was to leverage UDP sockets to develop a reliable transport protocol ("rUDP") operating at the Application layer. Although distinct from TCP, rUDP emulates TCP's reliability by ensuring dependable delivery of UDP datagrams.

The rUDP protocol consists of two components: `rSender` and `rReceiver`. 

The role of `rSender` is to read a specified file from the system and transmit it to the designated `rReceiver` address. Data from the input file is read in chunks, adhering to the maximum frame size for Ethernet (1500 bytes, including protocol headers). Each chunk is appended with a checksum, computed using a 32-bit CRC header, enabling detection of packet corruption. `rSender` uses a sliding window system to achieve reliable data transfer. A packet buffer stores unacknowledged packets, and if the time since the last acknowledgment surpasses the 500ms retransmission time, all packets within the window are retransmitted.

The connected `rReceiver` mirrors much of the functionality of `rSender`. After establishing a connection, `rReceiver` creates a new binary output file and begins the process of writing received data into the file. For each data packet, if the checksum is valid and the sequence number falls within the current window, the packet is stored in a buffer. The packet buffer is continuously scanned; consecutive packets with the expected sequence number are written to the output file, while out-of-order packets trigger an acknowledgment to the `rSender` containing the sequence number for the next expected packet. 

#### Result

The resulting program ensures dependable file transmission between hosts using UDP sockets. While UDP itself is not a reliable transport protocol, rUDP provides resilience against packet loss, packet corruption, duplication of packets, and re-ordered/delayed acknowledgment arrivals.

### Project 3: BitTorrent Lite

#### Description

Having gained some familiarity with socket programming, Project 3 pushed us to use knowledge from the previous projects to write a more sophisticated program. The objective was to implement a simplified version of BitTorrent ("BitTorrent Lite"), a peer-to-peer file-sharing protocol.

BitTorrent Lite consists of two components: the `tracker` server and the `peer`(s). 

The tracker server acts as the overseer of the peer-to-peer file-sharing network. Knowing the identities of all peers in the network and the content of the file being shared, the tracker server provides torrent files to peers seeking to download a file. The torrent file contains the address of all network peers along with CRC32 hashes corresponding to each chunk of the desired file (indexed by chunk ID). Since TCP handles the retransmission of corrupted packets, the hash values in the torrent file are solely intended to mitigate the threat of malware injection by malicious peers. The tracker server generates the torrent file prior to file sharing initiation and can concurrently manage multiple torrent file requests from peers.

Peers are processes that wish to download a file from the peer-to-peer network. A peer begins by requesting the torrent file from the tracker server at the specified address. Upon reception, the peer parses the torrent file to identify network peers and determine the expected hash for each chunk of the desired file. The peer simultaneously *requests chunks* from other peers while responding to *requests for chunks* from peers in the network. After querying all network peers to ascertain their available chunks, the peer begins requesting chunks using a rarest-first approach. Upon receipt, each chunk's hash is validated against the torrent file; invalid chunks are discarded. Once all chunks listed in the torrent file have been received, they are written to an output file on the peer's host.

#### Result

The resultant program offers a method of file transmission that distributes the load of download requests, which are typically centralized on a few servers, across a much greater quantity of hosts. This approach generally enables content providers to reduce bandwidth usage while providing faster download speeds for users.
