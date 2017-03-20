// Copyright (c) 2016 Fiach Antaw
// SPDX-License-Identifier: BSL-1.0

#include <Poco/Error.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <poll.h>
#include <algorithm>
#include <memory>
#include <string>
#include <cstring>
#include <vector>
#include "IIOSupport.hpp"

/***********************************************************************
 * |PothosDoc IIO Sink
 *
 * The IIO source forwards an input sample stream to an IIO output device.
 *
 * |category /IIO
 * |category /Sinks
 * |keywords iio industrial io adc sdr
 *
 * |param deviceId[Device ID] The ID of an IIO device on the system.
 * |default ""
 *
 * |param channelIds[Channel IDs] The IDs of channels to enable.
 * If no IDs are specified, all channels will be enabled.
 * |preview disable
 * |default []
 *
 * |param enablePorts[Enable Ports] If true and compatible channels are
 * enabled, enable input ports. This option reserves the IIO buffer for this
 * device, and so can only be enabled for one IIO block per device.
 * |preview disable
 * |default True
 * |widget DropDown()
 * |option [True] True
 * |option [False] False
 * 
 * |factory /iio/sink(deviceId, channelIds, enablePorts)
 **********************************************************************/
class IIOSink : public Pothos::Block
{
private:
    std::unique_ptr<IIODevice> dev;
    std::unique_ptr<IIOBuffer> buf;
    std::vector<IIOChannel> channels;
    bool enablePorts;
public:
    IIOSink(const std::string &deviceId, const std::vector<std::string> &channelIds,
        const bool &enablePorts) : enablePorts(enablePorts)
    {
        //expose overlay hook
        this->registerCall(this, POTHOS_FCN_TUPLE(IIOSink, overlay));

        //get libiio context
        IIOContext& ctx = IIOContext::get();

        //if deviceId is blank, create a partial object that exposes the
        //overlay hook for the gui but cannot be activated
        if (deviceId == "") {
            return;
        }

        //find iio device
        for (auto d : ctx.devices())
        {
            if (d.id() == deviceId)
            {
                this->dev = std::unique_ptr<IIODevice>(new IIODevice(d));
                break;
            }
        }
        if (!this->dev)
        {
            throw Pothos::SystemException("IIOSink::IIOSink()", "device not found");
        }

        //set up probes/setters for device attributes 
        for (auto a : this->dev->attributes())
        {
            Pothos::Callable attrGetter(&IIOSink::getDeviceAttribute);
            Pothos::Callable attrSetter(&IIOSink::setDeviceAttribute);
            attrGetter.bind(std::ref(*this), 0);
            attrGetter.bind(a, 1);
            attrSetter.bind(std::ref(*this), 0);
            attrSetter.bind(a, 1);

            std::string getDeviceAttrName = "deviceAttribute[" + a.name() + "]";
            std::string setDeviceAttrName = "setdeviceAttribute[" + a.name() + "]";
            this->registerCallable(getDeviceAttrName, attrGetter);
            this->registerCallable(setDeviceAttrName, attrSetter);
            this->registerProbe(getDeviceAttrName);
        }

        //set up probes/ports for selected input channels
        for (auto c : this->dev->channels())
        {
            if (!c.isOutput())
                continue;
            std::string cId = c.id();
            if (channelIds.size() > 0 && std::none_of(channelIds.begin(), channelIds.end(),
                    [cId](std::string s){ return s == cId; }))
                continue;
            this->channels.push_back(c);

            //set up input ports for scannable input channels
            if (c.isScanElement() && this->enablePorts)
            {
                this->setupInput(c.id(), c.dtype());
            }

            //set up probes/setters for channel attributes
            for (auto a : c.attributes())
            {
                Pothos::Callable attrGetter(&IIOSink::getChannelAttribute);
                Pothos::Callable attrSetter(&IIOSink::setChannelAttribute);
                attrGetter.bind(std::ref(*this), 0);
                attrGetter.bind(a, 1);
                attrSetter.bind(std::ref(*this), 0);
                attrSetter.bind(a, 1);

                std::string getChannelAttrName = "channelAttribute[" + c.id() + "][" + a.name() + "]";
                std::string setChannelAttrName = "setChannelAttribute[" + c.id() + "][" + a.name() + "]";
                this->registerCallable(getChannelAttrName, attrGetter);
                this->registerCallable(setChannelAttrName, attrSetter);
                this->registerProbe(getChannelAttrName);
            }
        }
    }

    std::string overlay(void) const
    {
        IIOContext& ctx = IIOContext::get();

        Poco::JSON::Object::Ptr topObj(new Poco::JSON::Object());

        Poco::JSON::Array::Ptr params(new Poco::JSON::Array());
        topObj->set("params", params);

        //configure deviceId dropdown options
        Poco::JSON::Object::Ptr deviceIdParam(new Poco::JSON::Object());
        params->add(deviceIdParam);
        Poco::JSON::Array::Ptr deviceIdOpts(new Poco::JSON::Array());
        deviceIdParam->set("key", "deviceId");
        deviceIdParam->set("options", deviceIdOpts);
        Poco::JSON::Object::Ptr deviceIdWidgetKwargs(new Poco::JSON::Object());
        deviceIdWidgetKwargs->set("editable", false);
        deviceIdParam->set("widgetKwargs", deviceIdWidgetKwargs);
        deviceIdParam->set("widgetType", "DropDown");

        //add empty device option associated
        Poco::JSON::Object::Ptr emptyOption(new Poco::JSON::Object());
        emptyOption->set("name", "");
        emptyOption->set("value", "\"\"");
        deviceIdOpts->add(emptyOption);

        //enumerate iio devices
        for (auto d : ctx.devices())
        {
            //use the standard label convention, but fall-back on driver/serial
            std::string name;

            Poco::JSON::Object::Ptr option(new Poco::JSON::Object());
            option->set("name", d.name() + " (" + d.id() + ")");
            option->set("value", "\"" + d.id() + "\"");
            deviceIdOpts->add(option);
        }

        std::stringstream ss;
        topObj->stringify(ss, 4);
        return ss.str();
    }

    static Block *make(const std::string &deviceId, const std::vector<std::string> &channelIds,
        const bool &enablePorts)
    {
        return new IIOSink(deviceId, channelIds, enablePorts);
    }

    std::string getDeviceAttribute(IIOAttr<IIODevice> a)
    {
        return a.value();
    }

    void setDeviceAttribute(IIOAttr<IIODevice> a, Pothos::Object value)
    {
        a = value.toString();
    }

    std::string getChannelAttribute(IIOAttr<IIOChannel> a)
    {
        return a.value();
    }

    void setChannelAttribute(IIOAttr<IIOChannel> a, Pothos::Object value)
    {
        a = value.toString();
    }

    void activate(void)
    {
        if (!this->dev)
        {
            throw Pothos::SystemException("IIOSink::activate()", "no device specified");
        }

        bool haveScanElements = false;
        if (this->buf) {
            this->buf.reset();
        }

        for (auto c : this->channels)
        {
            c.enable();

            if (c.isScanElement())
            {
                haveScanElements = true;
            }
        }

        //create sample buffer if we've got any scan elements
        //buffer size defaults to 4096 samples per buffer, for now
        if (haveScanElements && this->enablePorts) {
            this->buf = std::unique_ptr<IIOBuffer>(new IIOBuffer(std::move(this->dev->createBuffer(4096, false))));
            if (!this->buf)
            {
                throw Pothos::SystemException("IIOSink::activate()", "buffer creation failed");
            }
            this->buf->setBlockingMode(false);
        }
    }

    void deactivate(void)
    {
        if (this->buf) {
            this->buf.reset();
        }
    }

    void work(void)
    {
        auto sample_count = this->workInfo().minInElements;
 
        if (this->buf) {
            //wait for samples
            struct pollfd pfd = {
                .fd = this->buf->fd(),
                .events = POLLOUT,
                .revents = 0
            };
            struct timespec ts = {
                .tv_sec = static_cast<time_t>(this->workInfo().maxTimeoutNs/10000000),
                .tv_nsec = static_cast<long int>(this->workInfo().maxTimeoutNs % 10000000)
            };
            int ret = ppoll(&pfd, 1, &ts, NULL);
            if (ret < 0)
                throw Pothos::SystemException("IIOSource::work()", "ppoll failed: " + Poco::Error::getMessage(-ret));
            else if (ret == 0)
                return this->yield();

            //consume samples
            for (auto c : this->channels)
            {
                if (c.isScanElement()) {
                    auto inputPort = this->input(c.id());
                    auto inputBuffer = inputPort->buffer();

                    c.write(*this->buf, inputBuffer.as<void*>(), sample_count);
                    inputPort->consume(sample_count);
                }
            }

            //push new samples to iio device
            this->buf->push(sample_count);
        }
    }
};

static Pothos::BlockRegistry registerIIOSink(
    "/iio/sink", &IIOSink::make);
