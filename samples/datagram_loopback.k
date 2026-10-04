/*
 * One packet with ephemeral ports and a copied source address.
 * Copyright 2026 Emilien Kia. Licensed under the Apache License, Version 2.0.
 */
module datagram_loopback;
main() : int throws(Throwable) {
    deadline : Duration = Duration::ofSeconds(5L);
    receiver : k::io::DatagramSocket! = k::io::DatagramSocket::open();
    sender : k::io::DatagramSocket! = k::io::DatagramSocket::open();
    remote : k::io::NetworkAddress("127.0.0.1", receiver->localPort());
    input : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(2u);
    output : k::io::ByteBuffer! = k::io::ByteBuffer::allocateDirect(2u);
    output->put((byte) 79); output->put((byte) 75); output->flip();
    receive : Future<k::io::DatagramReceive>! = receiver->receiveAsync(input, deadline);
    sent : int = sender->sendToSync(output, remote, deadline);
    packet : k::io::DatagramReceive = receive->get(deadline);
    valid : bool = sent == 2 && packet.bytesTransferred() == 2 &&
        packet.datagramLength() == 2L && !packet.isTruncated() &&
        packet.source().port() == sender->localPort() &&
        input->get(0u) == (byte) 79 && input->get(1u) == (byte) 75;
    sender->closeSync(); receiver->closeSync();
    delete receive; delete input; delete output; delete sender; delete receiver;
    return valid ? 0 : 1;
}
