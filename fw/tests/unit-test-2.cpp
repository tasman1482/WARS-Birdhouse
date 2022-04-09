#include <Arduino.h>

#include "../WARS-Birdhouse/CircularBuffer.h"
#include "../WARS-Birdhouse/packets.h"
#include "../WARS-Birdhouse/OutboundPacketManager.h"
#include "../WARS-Birdhouse/Clock.h"
#include "../WARS-Birdhouse/Instrumentation.h"
#include "../WARS-Birdhouse/RoutingTable.h"
#include "../WARS-Birdhouse/RoutingTableImpl.h"
#include "../WARS-Birdhouse/MessageProcessor.h"
#include "../WARS-Birdhouse/CommandProcessor.h"
#include "../WARS-Birdhouse/Configuration.h"

#include <iostream>
#include <assert.h>
#include <string.h>

using namespace std;

// ===== DUMMY COMPONENTS =============================================

class TestStream : public Stream {
public:

    void print(const char* m) { cout << m; }
    void print(unsigned int m) { cout << m; }
    void print(uint16_t m) { cout << m; }
    void println() { cout << endl; }
    void println(const char* m) { cout << m << endl; }
};

class TestClock : public Clock {
public:

    TestClock() {
        _time = 10 * 1000;
    }

    uint32_t time() const {
        return _time;
    };

    void setTime(uint32_t t) {
        _time = t;
    }

    void advanceSeconds(uint32_t seconds) {
        _time += (seconds * 1000);
    }

private:

    uint32_t _time;
};

class TestInstrumentation : public Instrumentation {
public:
    uint16_t getSoftwareVersion() const { return 1; }
    uint16_t getDeviceClass() const { return 2; }
    uint16_t getDeviceRevision() const { return 1; }
    uint16_t getBatteryVoltage() const { return 3800; }
    uint16_t getPanelVoltage() const { return 4000; }
    int16_t getTemperature() const { return 23; }
    int16_t getHumidity() const { return  87; }
    uint16_t getBootCount() const { return 1; }
    uint16_t getSleepCount() const { return 1; }
    void restart() { cout << "RESTART" << endl; }
    void restartRadio() { cout << "RESTART" << endl; }
    void sleep(uint32_t ms) { cout << "SLEEP " << ms << endl; }
};
/*
class TestRoutingTable : public RoutingTable {
public:
    
    TestRoutingTable() {
        clearRoutes();
    }

    nodeaddr_t nextHop(nodeaddr_t finalDestAddr) {
        if (finalDestAddr == 0) {
            return 0;
        } else if (finalDestAddr >= 0xfff0) {
            return finalDestAddr;
        } else if (finalDestAddr >= 64) {
            return NO_ROUTE;
        } else {
            return _table[finalDestAddr];
        }
    }

    void setRoute(nodeaddr_t target, nodeaddr_t nextHop) {
        _table[target] = nextHop;
    }

    void clearRoutes() {
        for (unsigned int i = 0; i < 64; i++)
            _table[i] = RoutingTable::NO_ROUTE;
    }

private:

    nodeaddr_t _table[64];
};
*/
class TestConfiguration : public Configuration {
public:

    TestConfiguration(nodeaddr_t myAddr, const char* myCall) 
    : _myAddr(myAddr), 
      _myCall(myCall) {
    }

    nodeaddr_t getAddr() const {
        return _myAddr;
    }

    CallSign getCall() const {
        return _myCall;
    }

    uint16_t getBatteryLimit() const {
        return 3400;
    }

private:

    nodeaddr_t _myAddr;
    const CallSign _myCall;
};

void movePacket(CircularBuffer& from, CircularBuffer& to) {
    unsigned int packetLen = 256;
    uint8_t packet[256];
    bool got = from.popIfNotEmpty(0, packet, &packetLen);
    if (got) {
        int16_t rssi = 100;
        to.push(&rssi, packet, packetLen);
    }
}

// ===== TEST CASES ================================================

static TestStream testStream;

TestClock systemClock;

// Node #1
TestConfiguration testConfig(1, "KC1FSZ");
TestInstrumentation testInstrumentation;
//static TestRoutingTable testRoutingTable;
static RoutingTableImpl testRoutingTable;
CircularBufferImpl<4096> testTxBuffer(0);
CircularBufferImpl<4096> testRxBuffer(2);
static MessageProcessor testMessageProcessor(systemClock, testRxBuffer, testTxBuffer,
    testRoutingTable, testInstrumentation, testConfig,
    10 * 1000, 2 * 1000);

// Exposed base interfaces to the rest of the program
Stream& logger = testStream;
Configuration& systemConfig = testConfig;
Instrumentation& systemInstrumentation = testInstrumentation;
RoutingTable& systemRoutingTable = testRoutingTable;
MessageProcessor& systemMessageProcessor = testMessageProcessor;

void test_CommandProcessor() {
    
    systemRoutingTable.setRoute(3, 3);
    systemRoutingTable.setRoute(7, 3);

    // PING
    {
        const char* a0 = "ping";
        const char* a1 = "7";
        const char *a_args[2] = { a0, a1 };

        sendPing(2, a_args);

        systemMessageProcessor.pump();

        // Make sure we see the outbound message
        assert(!testTxBuffer.isEmpty());
        testTxBuffer.popAndDiscard();
    }

    // INFO
    {
        const char* a0 = "info";
        const char *a_args[2] = { a0 };

        info(1, a_args);

        systemMessageProcessor.pump();

        // Make sure we dont see outbound message
        assert(testTxBuffer.isEmpty());
    }

    // SET ROUTE
    {
        const char* a0 = "setroute";
        const char* a1 = "8";
        const char* a2 = "3";
        const char *a_args[3] = { a0, a1, a2 };

        setRoute(3, a_args);

        systemMessageProcessor.pump();

        // Make sure we dont see the outbound message
        assert(testTxBuffer.isEmpty());

        // Check the routing table
        assert(systemRoutingTable.nextHop(8) == 3);
    }

    // SET ROUTE REMOTE
    {
        const char* a0 = "setrouteremote";
        const char* a1 = "7";
        const char* a2 = "1";
        const char* a3 = "4";
        const char *a_args[4] = { a0, a1, a2, a3 };

        sendSetRoute(4, a_args);

        systemMessageProcessor.pump();

        // Make sure we see the outbound message
        assert(!testTxBuffer.isEmpty());

        // Pull off the message and examine it
        Packet packet;
        unsigned int packetLen = sizeof(packet);
        testTxBuffer.pop(0, (void*)&packet, &packetLen);
        assert(packet.header.getType() == TYPE_SETROUTE);
        assert(packet.header.destAddr == 3);
        assert(packet.header.sourceAddr == 1);

        // Look at payload
        SetRouteReqPayload payload;
        memcpy((void*)&payload, packet.payload, sizeof(payload));
        assert(payload.targetAddr == 1);
        assert(payload.nextHopAddr == 4);
    }

    // SEND TEXT
    {
        const char* a0 = "text";
        const char* a1 = "7";
        const char* a2 = "Hello World!";
        const char *a_args[3] = { a0, a1, a2 };

        sendText(3, a_args);

        systemMessageProcessor.pump();

        // Make sure we see the outbound message
        assert(!testTxBuffer.isEmpty());

        // Pull off the message and examine it
        Packet packet;
        unsigned int packetLen = sizeof(packet);
        testTxBuffer.pop(0, (void*)&packet, &packetLen);

        assert(packet.header.getType() == TYPE_TEXT);
        assert(packet.header.destAddr == 3);
        assert(packet.header.sourceAddr == 1);
        assert(packetLen == 12 + sizeof(Header));

        // Look at payload
        assert(memcmp(packet.payload, "Hello World!", 12) == 0);
    }
}

int main(int arg, const char** argv) {
    test_CommandProcessor();
}
