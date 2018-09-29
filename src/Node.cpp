#include "Node.hpp"

#include <QtCore/QObject>

#include <utility>
#include <iostream>

#include "FlowScene.hpp"

#include "NodeGraphicsObject.hpp"
#include "NodeDataModel.hpp"

#include "ConnectionGraphicsObject.hpp"
#include "ConnectionState.hpp"

using QtNodes::Node;
using QtNodes::NodeGeometry;
using QtNodes::NodeState;
using QtNodes::NodeData;
using QtNodes::NodeDataType;
using QtNodes::NodeDataModel;
using QtNodes::NodeGraphicsObject;
using QtNodes::PortIndex;
using QtNodes::PortType;

Node::
Node(std::unique_ptr<NodeDataModel> && dataModel)
  : _uid(QUuid::createUuid())
  , _nodeDataModel(std::move(dataModel))
  , _nodeState(_nodeDataModel)
  , _nodeGeometry(_nodeDataModel)
  , _nodeGraphicsObject(nullptr)
{
  _nodeGeometry.recalculateSize();

  // propagate data: model => node
  connect(_nodeDataModel.get(), &NodeDataModel::dataUpdated,
          this, &Node::onDataUpdated);

  connect(_nodeDataModel.get(), &NodeDataModel::portAdded,
	  this, &Node::onPortAdded);

  connect(_nodeDataModel.get(), &NodeDataModel::portMoved,
	  this, &Node::onPortMoved);

  connect(_nodeDataModel.get(), &NodeDataModel::portRemoved,
	  this, &Node::onPortRemoved);
}


Node::
~Node() = default;

QJsonObject
Node::
save() const
{
  QJsonObject nodeJson;

  nodeJson["id"] = _uid.toString();

  nodeJson["model"] = _nodeDataModel->save();

  QJsonObject obj;
  obj["x"] = _nodeGraphicsObject->pos().x();
  obj["y"] = _nodeGraphicsObject->pos().y();
  nodeJson["position"] = obj;

  return nodeJson;
}


void
Node::
restore(QJsonObject const& json)
{
  _uid = QUuid(json["id"].toString());

  QJsonObject positionJson = json["position"].toObject();
  QPointF     point(positionJson["x"].toDouble(),
                    positionJson["y"].toDouble());
  _nodeGraphicsObject->setPos(point);

  _nodeDataModel->restore(json["model"].toObject());
}


QUuid
Node::
id() const
{
  return _uid;
}


void
Node::
reactToPossibleConnection(PortType reactingPortType,
                          NodeDataType const &reactingDataType,
                          QPointF const &scenePoint)
{
  QTransform const t = _nodeGraphicsObject->sceneTransform();

  QPointF p = t.inverted().map(scenePoint);

  _nodeGeometry.setDraggingPosition(p);

  _nodeGraphicsObject->update();

  _nodeState.setReaction(NodeState::REACTING,
                         reactingPortType,
                         reactingDataType);
}


void
Node::
resetReactionToConnection()
{
  _nodeState.setReaction(NodeState::NOT_REACTING);
  _nodeGraphicsObject->update();
}


NodeGraphicsObject const &
Node::
nodeGraphicsObject() const
{
  return *_nodeGraphicsObject.get();
}


NodeGraphicsObject &
Node::
nodeGraphicsObject()
{
  return *_nodeGraphicsObject.get();
}


void
Node::
setGraphicsObject(std::unique_ptr<NodeGraphicsObject>&& graphics)
{
  _nodeGraphicsObject = std::move(graphics);

  _nodeGeometry.recalculateSize();
}


NodeGeometry&
Node::
nodeGeometry()
{
  return _nodeGeometry;
}


NodeGeometry const&
Node::
nodeGeometry() const
{
  return _nodeGeometry;
}


NodeState const &
Node::
nodeState() const
{
  return _nodeState;
}


NodeState &
Node::
nodeState()
{
  return _nodeState;
}


NodeDataModel*
Node::
nodeDataModel() const
{
  return _nodeDataModel.get();
}


void
Node::
propagateData(std::shared_ptr<NodeData> nodeData,
              PortIndex inPortIndex) const
{
  _nodeDataModel->setInData(std::move(nodeData), inPortIndex);

  //Recalculate the nodes visuals. A data change can result in the node taking more space than before, so this forces a recalculate+repaint on the affected node
  updateGraphics();
}


void
Node::
onDataUpdated(PortIndex index)
{
  auto nodeData = _nodeDataModel->outData(index);

  auto connections =
    _nodeState.connections(PortType::Out, index);

  for (auto const & c : connections)
    c.second->propagateData(nodeData);
}

void
Node::
updateGraphics() const
{
	_nodeGraphicsObject->setGeometryChanged();
	_nodeGeometry.recalculateSize();
	_nodeGraphicsObject->update();
	_nodeGraphicsObject->moveConnections();
}

void
Node::
insertEntry(PortType portType, PortIndex index)
{
	// Insert new port
	auto& entries = _nodeState.getEntries(portType);
	entries.insert(entries.begin() + index, NodeState::ConnectionPtrSet());

	// Move subsequent port indices up by one
	for (int i = index + 1; i < entries.size(); ++i)
	{
		for (const auto& value : entries[i])
		{
			Connection* connection = value.second;
			Node* node = connection->getNode(portType);
			if (node)
			{
				PortIndex newIndex = connection->getPortIndex(portType) + 1;
				connection->setNodeToPort(*node, portType, newIndex);
			}
		}
	}
}

void
Node::
eraseEntry(PortType portType, PortIndex index)
{
	auto& entries = _nodeState.getEntries(portType);
	entries.erase(entries.begin() + index);

	// Move subsequent port indices down by one
	for (int i = index; i < entries.size(); ++i)
	{
		for (const auto& value : entries[i])
		{
			Connection* connection = value.second;
			Node* node = connection->getNode(portType);
			if (node)
			{
				PortIndex newIndex = connection->getPortIndex(portType) - 1;
				connection->setNodeToPort(*node, portType, newIndex);
			}
		}
	}
}

void
Node::
onPortAdded(PortType portType, PortIndex index)
{
	insertEntry(portType, index);

	updateGraphics();
}

void
Node::
onPortMoved(PortType portType, PortIndex oldIndex, PortIndex newIndex)
{
	// Remove port
	auto& entries = _nodeState.getEntries(portType);
	auto connections = entries[oldIndex];

	eraseEntry(portType, oldIndex);
	insertEntry(portType, newIndex);

	updateGraphics();
}

void
Node::
onPortRemoved(PortType portType, PortIndex index)
{
  // Remove connections to this port
  auto& entries = _nodeState.getEntries(portType);
	for (int i = _nodeDataModel->nPorts(portType); i < entries.size(); ++i)
	{
		std::vector<Connection*> connections;
		for (const auto& value : entries[index])
			connections.push_back(value.second);

		// connections may be removed from entries in connectionRemoved()
		for (Connection* connection : connections)
			emit connectionRemoved(*connection);
	}

	eraseEntry(portType, index);

	updateGraphics();
}